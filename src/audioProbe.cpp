#include "audioProbe.h"
#include "RtAudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

// ASIOControlPanel e o wrapper que o ASIO SDK traz para dentro da rtAudio.lib:
// o simbolo ?ASIOControlPanel@@YAJXZ foi conferido como membro publico do lib
// antes de esta linha existir, e e por ela que o painel do ASIO4ALL se abre.
// So pode ser chamada com um driver ASIO carregado - por dentro ela usa o
// ponteiro global do driver, e sem driver carregado nao ha retorno possivel.
long ASIOControlPanel();

namespace {

	// Cada linha sai por stderr assim que existe. O cv0Log do ofApp e local
	// aquele ficheiro e o log do OF nesta build mata o processo na primeira
	// escrita (ver main.cpp), entao a sonda traz o seu proprio escritor.
	void apLog(const std::string& s) {
		fprintf(stderr, "[audio] %s\n", s.c_str());
		fflush(stderr);
	}

	// Uma rota pode derrubar o processo. Sem impressao incremental o que ja
	// foi medido morre junto com ela, por isso cada API fala por si no fim do
	// proprio turno e outra vez no fim da rota.
	const double JANELA_S = 1.5;
	const int CAND_BUFFER[] = { 512, 256, 1024 };
	const size_t MAX_TENTATIVAS_ROTA = 40;

	struct CbState {
		std::atomic<long> callbacks{ 0 };
		std::atomic<long> underflow{ 0 };
		std::atomic<long> relogio_paro{ 0 };
		std::atomic<long long> nanos{ 0 };
		long long bytesFrame = 8;   // canais x bytes por amostra
		double ultimo = -1.0;       // so a thread do callback le e escreve
	};

	int probeCallback(void* out, void* in, unsigned nFrames, double streamTime,
		RtAudioStreamStatus status, void* userData) {
		CbState* st = static_cast<CbState*>(userData);
		(void)in;
		const auto t0 = std::chrono::steady_clock::now();

		// streamTime parado e o motorista a chamar-nos sem relogio nenhum: e o
		// jeito de um transporte "vivo" que nao anda.
		if (st->ultimo == streamTime) st->relogio_paro.fetch_add(1);
		st->ultimo = streamTime;
		if (status & RTAUDIO_OUTPUT_UNDERFLOW) st->underflow.fetch_add(1);

		// silencio absoluto: a sonda mede transporte, nao emite som nenhum
		if (out && nFrames > 0) memset(out, 0, (size_t)nFrames * (size_t)st->bytesFrame);

		st->callbacks.fetch_add(1);
		const auto dt = std::chrono::steady_clock::now() - t0;
		st->nanos.fetch_add((long long)std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
		return 0;   // nunca pedir stop nem abort
	}

	std::string erroNome(RtAudioErrorType t) {
		switch (t) {
		case RTAUDIO_NO_ERROR: return "sem erro";
		case RTAUDIO_WARNING: return "aviso";
		case RTAUDIO_UNKNOWN_ERROR: return "erro desconhecido";
		case RTAUDIO_NO_DEVICES_FOUND: return "nenhum dispositivo";
		case RTAUDIO_INVALID_DEVICE: return "dispositivo invalido";
		case RTAUDIO_DEVICE_DISCONNECT: return "dispositivo desconectado";
		case RTAUDIO_MEMORY_ERROR: return "erro de memoria";
		case RTAUDIO_INVALID_PARAMETER: return "parametro invalido";
		case RTAUDIO_INVALID_USE: return "uso invalido";
		case RTAUDIO_DRIVER_ERROR: return "erro de driver";
		case RTAUDIO_SYSTEM_ERROR: return "erro de sistema";
		case RTAUDIO_THREAD_ERROR: return "erro de thread";
		}
		return "erro " + std::to_string((int)t);
	}

	std::string formatoNome(RtAudioFormat f) {
		if (f == RTAUDIO_FLOAT32) return "float32";
		if (f == RTAUDIO_SINT16) return "int16";
		if (f == RTAUDIO_SINT32) return "int32";
		if (f == RTAUDIO_FLOAT64) return "float64";
		return "fmt" + std::to_string((unsigned long)f);
	}

	int bytesPorAmostra(RtAudioFormat f) {
		if (f == RTAUDIO_FLOAT32 || f == RTAUDIO_SINT32) return 4;
		if (f == RTAUDIO_SINT16) return 2;
		if (f == RTAUDIO_FLOAT64) return 8;
		return 3;
	}

	// taxas candidatas: o que o dispositivo prefere, o que ele usa agora, e o
	// resto da lista dele, sem repeticao e com teto de 4 - sem teto a varredura
	// de taxa x formato x buffer cresce demais para uma espera de palco.
	void taxasCandidatas(const RtAudio::DeviceInfo& d, std::vector<unsigned int>& out) {
		out.clear();
		const unsigned int semente[] = { d.preferredSampleRate, d.currentSampleRate };
		for (unsigned int s : semente) {
			if (s > 0 && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
		}
		for (unsigned int s : d.sampleRates) {
			if (s > 0 && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
			if (out.size() >= 4) break;
		}
		if (out.empty()) out.push_back(48000);
	}

	// formatos candidatos: float32 primeiro porque e o que o motor de audio do
	// OF usa por dentro; int16 depois porque e o que um driver velho costuma
	// expor. Nada alem disso na sonda.
	void formatosCandidatos(const RtAudio::DeviceInfo& d, std::vector<RtAudioFormat>& out) {
		out.clear();
		if (d.nativeFormats & RTAUDIO_FLOAT32) out.push_back(RTAUDIO_FLOAT32);
		if (d.nativeFormats & RTAUDIO_SINT16) out.push_back(RTAUDIO_SINT16);
		if (out.empty()) out.push_back(RTAUDIO_FLOAT32);
	}

	std::string envStr(const char* name) {
		char buf[64] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), name) != 0 || got == 0) return std::string();
		return std::string(buf);
	}

	bool bateComFiltro(const std::string& token, const std::string& filtro) {
		return filtro.empty() || filtro == "all" || token == filtro;
	}

	// UMA API ate ao fim. Nada aqui toca em servico, registo ou ficheiro: so
	// pede ao RtAudio o que ele consegue abrir.
	void runRoute(AudioRoute& r, RtAudio::Api api, const std::string& token, const std::string& filtro) {
		r.api = token;
		r.pedidos = token;
		if (!bateComFiltro(token, filtro)) return;   // pedido para ficar fora desta execucao
		r.pediu = true;

		std::string ultimoErro;
		RtAudio ra(api, [&](RtAudioErrorType t, const std::string& e) {
			ultimoErro = erroNome(t) + ": " + e;
		});

		if (ra.getCurrentApi() != api) {
			r.motivo = "nao compilado nesta lib; RtAudio trocou por "
				+ RtAudio::getApiName(ra.getCurrentApi());
			return;
		}
		r.compilado = true;

		const std::vector<unsigned int> ids = ra.getDeviceIds();
		r.dispositivos = (int)ids.size();
		for (unsigned int id : ids) {
			const RtAudio::DeviceInfo d = ra.getDeviceInfo(id);
			if (!r.nomes.empty()) r.nomes += " | ";
			r.nomes += d.name + "[" + std::to_string(id) + "]";
			r.nomes += "/" + std::to_string(d.outputChannels) + "out";
			if (d.preferredSampleRate > 0)
				r.nomes += "@" + std::to_string(d.preferredSampleRate);
		}

		for (unsigned int id : ids) {
			RtAudio::DeviceInfo d = ra.getDeviceInfo(id);
			if (d.outputChannels == 0) continue;

			std::vector<unsigned int> taxas;
			std::vector<RtAudioFormat> fmts;
			taxasCandidatas(d, taxas);
			formatosCandidatos(d, fmts);

			RtAudio::StreamParameters p;
			p.deviceId = d.ID;
			p.nChannels = std::min(d.outputChannels, 2u);
			p.firstChannel = 0;

			bool abriu = false;
			for (unsigned int taxa : taxas) {
				for (RtAudioFormat fmt : fmts) {
					for (int cand : CAND_BUFFER) {
						if (r.tentativas >= MAX_TENTATIVAS_ROTA) break;
						++r.tentativas;
						unsigned int buf = (unsigned int)cand;
						RtAudio::StreamOptions opt;
						opt.flags = 0;
						opt.numberOfBuffers = 0;   // o sistema escolhe: a sonda quer o default
						CbState st;
						st.bytesFrame = (long long)p.nChannels * bytesPorAmostra(fmt);

						const RtAudioErrorType e = ra.openStream(&p, nullptr, fmt, taxa,
							&buf, &probeCallback, &st, &opt);
						if (e != RTAUDIO_NO_ERROR) {
							r.motivo = erroNome(e) + ": " + ra.getErrorText();
							continue;
						}

						const RtAudioErrorType s = ra.startStream();
						if (s != RTAUDIO_NO_ERROR && s != RTAUDIO_WARNING) {
							r.motivo = "abriu mas nao comecou - " + erroNome(s) + ": " + ra.getErrorText();
							ra.closeStream();
							continue;
						}

						const auto wall0 = std::chrono::steady_clock::now();
						std::this_thread::sleep_for(std::chrono::milliseconds((int)(JANELA_S * 1000)));
						const double parede = std::chrono::duration<double>(
							std::chrono::steady_clock::now() - wall0).count();
						const double relogio = ra.getStreamTime();

						r.callbacks = st.callbacks.load();
						r.underflow = st.underflow.load();
						r.relogio_paro = st.relogio_paro.load();
						r.parede = parede;
						r.relogio = relogio;
						r.latencia_ms = relogio > 0.0
							? 1000.0 * (double)ra.getStreamLatency() / (double)taxa : 0.0;
						r.ms_callback = r.callbacks > 0
							? (double)st.nanos.load() / 1.0e6 / (double)r.callbacks : 0.0;
						r.buffers = (int)opt.numberOfBuffers;
						r.abriu = d.name;
						r.taxa = (long)taxa;
						r.buffer = (int)buf;
						r.canais = (int)p.nChannels;
						r.formato = formatoNome(fmt);
						abriu = true;

						ra.stopStream();
						ra.closeStream();
						break;
					}
					if (abriu || r.tentativas >= MAX_TENTATIVAS_ROTA) break;
				}
				if (abriu || r.tentativas >= MAX_TENTATIVAS_ROTA) break;
			}
			if (abriu) break;   // uma rota viva por API basta para a decisao
		}
	}

	void falaRota(const AudioRoute& r) {
		if (!r.pediu) {
			apLog(r.api + ": fora desta execucao (CV0_AUDIOPROBE_API)");
			return;
		}
		if (!r.compilado) {
			apLog(r.api + ": " + r.motivo);
			return;
		}
		apLog(r.api + ": dispositivos " + std::to_string(r.dispositivos)
			+ (r.nomes.empty() ? " (nenhum nome)" : "  " + r.nomes));
		if (!r.abriu.empty() && r.callbacks > 0) {
			apLog(r.api + ": ABRIU " + r.abriu + "  " + std::to_string(r.taxa) + " Hz "
				+ std::to_string(r.buffer) + " quadros x" + std::to_string(r.buffers)
				+ " buf  " + r.formato + "  " + std::to_string(r.canais) + " canais");
			apLog(r.api + ": callbacks " + std::to_string(r.callbacks) + " em "
				+ std::to_string(r.parede).substr(0, 4) + " s = "
				+ std::to_string((long long)(r.parede > 0 ? r.callbacks / r.parede : 0.0))
				+ "/s | relogio do stream " + std::to_string(r.relogio).substr(0, 4)
				+ " s | underflow " + std::to_string(r.underflow)
				+ " | relogio parado " + std::to_string(r.relogio_paro)
				+ " | latencia " + std::to_string((long long)r.latencia_ms) + " ms"
				+ " | dentro do callback " + std::to_string(r.ms_callback).substr(0, 5) + " ms");
		} else if (!r.abriu.empty()) {
			apLog(r.api + ": ABRIU " + r.abriu + " mas com ZERO callbacks em "
				+ std::to_string(r.parede).substr(0, 4) + " s | VACUO: a rota nao chamou o motorista"
				+ " | relogio " + std::to_string(r.relogio).substr(0, 4) + " s");
		} else {
			apLog(r.api + ": nenhuma combinacao abriu (" + std::to_string(r.tentativas)
				+ " tentativas) | ultimo motivo: " + (r.motivo.empty() ? "sem erro reportado" : r.motivo));
		}
	}

	// ==================== teste de emissao ====================
	// O sinus vive num contador de amostras, nao num acumulador de fase em
	// ponto flutuante: somar um incremento por amostra durante cinco segundos a
	// 48 kHz perderia o ultimo digito e o que se ouviria era um batimento do
	// proprio acumulo, nao do transporte. Assim a frequencia e exata por
	// construcao e o pico medido e o pico escrito.
	const double EMIT_PI = 3.14159265358979323846;
	const double EMIT_HZ = 440.0;
	const double EMIT_DBFS = -18.0;
	const int EMIT_BUFFER = 512;
	const double EMIT_MIN_S = 1.0;
	const double EMIT_MAX_S = 30.0;

	struct EmitState {
		std::atomic<long> callbacks{ 0 };
		std::atomic<long> amostras{ 0 };
		long escrito = 0;         // so a thread do driver le e mexe
		double pico = 0.0;        // |amostra| maximo realmente escrita
		double taxa = 48000.0;
		double amp = 0.0;         // ja em contagem cheia do formato
		int canais = 1;
		bool flutuante = true;
	};

	int emitCallback(void* out, void* in, unsigned nFrames, double streamTime,
		RtAudioStreamStatus status, void* userData) {
		(void)in; (void)streamTime; (void)status;
		EmitState* st = static_cast<EmitState*>(userData);
		if (!out || nFrames == 0) return 0;

		double picoAqui = 0.0;
		if (st->flutuante) {
			float* p = static_cast<float*>(out);
			for (unsigned i = 0; i < nFrames; i++) {
				const long k = st->escrito + (long)i;
				const double v = st->amp * std::sin(2.0 * EMIT_PI * (double)k * EMIT_HZ / st->taxa);
				for (int c = 0; c < st->canais; c++) p[i * st->canais + c] = (float)v;
				const double a = v < 0.0 ? -v : v;
				if (a > picoAqui) picoAqui = a;
			}
		} else {
			short* p = static_cast<short*>(out);
			for (unsigned i = 0; i < nFrames; i++) {
				const long k = st->escrito + (long)i;
				const double v = st->amp * std::sin(2.0 * EMIT_PI * (double)k * EMIT_HZ / st->taxa);
				const short s = (short)(v + (v >= 0.0 ? 0.5 : -0.5));
				for (int c = 0; c < st->canais; c++) p[i * st->canais + c] = s;
				const double a = v < 0.0 ? -v : v;
				if (a > picoAqui) picoAqui = a;
			}
		}
		st->escrito += (long)nFrames;
		st->amostras.store(st->escrito);
		st->callbacks.fetch_add(1);
		if (picoAqui > st->pico) st->pico = picoAqui;
		return 0;
	}

	// Abre a primeira rota que abrir, pela mesma ordem da varredura - ASIO
	// primeiro porque e a unica que nao depende do servico morto - e emite.
	bool emitRota(RtAudio::Api api, const char* token, double segundos,
		double dbfs, EmitState& st, std::string& motivo, bool painel) {
		std::string ultimoErro;
		RtAudio ra(api, [&](RtAudioErrorType t, const std::string& e) {
			ultimoErro = erroNome(t) + ": " + e;
		});
		if (ra.getCurrentApi() != api) {
			motivo = std::string(token) + ": nao compilado";
			return false;
		}

		for (unsigned int id : ra.getDeviceIds()) {
			RtAudio::DeviceInfo d = ra.getDeviceInfo(id);
			if (d.outputChannels == 0) continue;

			std::vector<unsigned int> taxas;
			std::vector<RtAudioFormat> fmts;
			taxasCandidatas(d, taxas);
			formatosCandidatos(d, fmts);

			RtAudio::StreamParameters p;
			p.deviceId = d.ID;
			p.nChannels = std::min(d.outputChannels, 2u);
			p.firstChannel = 0;

			for (unsigned int taxa : taxas) {
				for (RtAudioFormat fmt : fmts) {
					unsigned int buf = EMIT_BUFFER;
					RtAudio::StreamOptions opt;
					opt.flags = 0;
					opt.numberOfBuffers = 0;
					st.taxa = (double)taxa;
					st.canais = (int)p.nChannels;
					st.flutuante = (fmt == RTAUDIO_FLOAT32);
					// a amplitude do teste e em relacao ao fundo do formato: um
					// float32 vai a 1,0 e um int16 a 32767, e -18 dBFS sao os
					// mesmos 0,12589 dos dois lados so depois desta conta
					st.amp = std::pow(10.0, dbfs / 20.0) * (st.flutuante ? 1.0 : 32767.0);
					// cada tentativa de abrir e uma contagem do zero: uma rota que
					// abriu e nao comecou deixaria atras si callbacks e amostras de
					// um stream que nunca existiu, e a leitura seguinte mentia
					st.escrito = 0;
					st.pico = 0.0;
					st.callbacks.store(0);
					st.amostras.store(0);

					const RtAudioErrorType e = ra.openStream(&p, nullptr, fmt, taxa,
						&buf, &emitCallback, &st, &opt);
					if (e != RTAUDIO_NO_ERROR) {
						motivo = std::string(token) + " " + d.name + ": "
							+ erroNome(e) + " " + ra.getErrorText();
						continue;
					}
					const RtAudioErrorType s = ra.startStream();
					if (s != RTAUDIO_NO_ERROR && s != RTAUDIO_WARNING) {
						motivo = std::string(token) + " " + d.name
							+ ": abriu mas nao comecou - " + erroNome(s);
						ra.closeStream();
						continue;
					}

					apLog(std::string("EMISSAO: ") + token + " | " + d.name + " | "
						+ std::to_string((long)taxa) + " Hz | " + formatoNome(fmt)
						+ " | " + std::to_string((int)p.nChannels) + " canais | buffer "
						+ std::to_string((int)buf) + " quadros");
					apLog(std::string("EMISSAO: ") + std::to_string((long)(segundos))
						+ " s de " + std::to_string((long)EMIT_HZ) + " Hz a "
						+ std::to_string(dbfs).substr(0, 5) + " dBFS = amplitude "
						+ std::to_string(st.amp).substr(0, 6)
						+ (st.flutuante ? " (contagem de float32)" : " (contagem de int16)"));

					// o painel e a unica superficie em que o mapeamento WDM do
					// ASIO4ALL se le sem adivinhar: o registo guarda hashes, nao
					// nomes. Pede-se por variavel porque uma janela modal nao e
					// coisa para aparecer num palco, e so faz sentido com um
					// driver ASIO ja carregado.
					if (painel && api == RtAudio::WINDOWS_ASIO) {
						apLog("EMISSAO: a abrir o painel do ASIO4ALL (ASIOControlPanel)...");
						const auto p0 = std::chrono::steady_clock::now();
						const long rc = ASIOControlPanel();
						const double pdur = std::chrono::duration<double>(
							std::chrono::steady_clock::now() - p0).count();
						apLog("EMISSAO: ASIOControlPanel devolveu " + std::to_string(rc)
							+ " em " + std::to_string(pdur).substr(0, 5) + " s"
							+ (pdur > 0.1 ? " | BLOQUEOU: a janela de medicao comeca depois do painel fechado"
								: " | nao bloqueou: o tom toca com o painel aberto"));
					} else if (painel) {
						apLog("EMISSAO: painel pedido mas a rota nao e ASIO - nada a abrir");
					}

					const auto wall0 = std::chrono::steady_clock::now();
					std::this_thread::sleep_for(std::chrono::milliseconds((int)(segundos * 1000)));
					const double parede = std::chrono::duration<double>(
						std::chrono::steady_clock::now() - wall0).count();
					const double relogio = ra.getStreamTime();
					const long amostras = st.amostras.load();

					apLog(std::string("EMISSAO: callbacks ") + std::to_string(st.callbacks.load())
						+ " em " + std::to_string(parede).substr(0, 4) + " s | relogio do stream "
						+ std::to_string(relogio).substr(0, 4) + " s | amostras escritas "
						+ std::to_string(amostras) + " (= " + std::to_string(relogio).substr(0, 4)
						+ " s x taxa) | pico escrito " + std::to_string(st.pico).substr(0, 6));

					// a conta que nao se pode poupar: se o relogio do stream nao
					// andou, o motorista chamou-nos mas o driver nao saiu de
					// lado nenhum, e um "pico escrito" alto nesse caso e um
					// numero bonito sobre silencio
					if (relogio < 0.2 * parede)
						apLog(std::string("EMISSAO: VACUO - o relogio do stream andou ")
							+ std::to_string(relogio).substr(0, 4) + " s em "
							+ std::to_string(parede).substr(0, 4)
							+ " s de parede. O pico escrito e nosso; o que o driver entregou e zero.");
					else if (amostras < (long)(0.5 * parede * st.taxa))
						apLog("EMISSAO: VACUO - amostras escritas muito abaixo da parede x taxa");

					ra.stopStream();
					ra.closeStream();
					return true;
				}
			}
		}
		if (motivo.empty()) motivo = std::string(token) + ": nenhum dispositivo com saidas";
		(void)ultimoErro;
		return false;
	}

}   // namespace anonimo

// ==================== porta publica ====================

bool audioProbeAtiva() {
	char buf[8] = { 0 };
	size_t got = 0;
	return getenv_s(&got, buf, sizeof(buf), "CV0_AUDIOPROBE") == 0 && got > 0;
}

void audioProbeRun(AudioProbeReport& rep) {
	rep.ativo = audioProbeAtiva();
	if (!rep.ativo) return;   // inerte: nenhuma linha daqui roda sem a variavel

	const std::string filtro = envStr("CV0_AUDIOPROBE_API");
	rep.versao = RtAudio::getVersion();

	std::vector<RtAudio::Api> comp;
	RtAudio::getCompiledApi(comp);
	for (RtAudio::Api a : comp) {
		if (!rep.compilados.empty()) rep.compilados += " ";
		rep.compilados += RtAudio::getApiName(a);
	}

	apLog("=== sonda de transporte (CV0_AUDIOPROBE) RtAudio " + rep.versao
		+ " | compilados: " + rep.compilados
		+ (filtro.empty() ? "" : " | filtro: " + filtro));

	// a ordem e a decisao que ficou do diagnostico: ASIO e a unica rota que nao
	// depende do AudioSrv, e WASAPI/DS vao estar mortos enquanto o servico
	// estiver no ciclo de quebra. Cada rota fala por si antes da proxima.
	const int ordem[] = { (int)RtAudio::WINDOWS_ASIO, (int)RtAudio::WINDOWS_WASAPI,
		(int)RtAudio::WINDOWS_DS };
	const char* tokens[] = { "asio", "wasapi", "ds" };

	for (int i = 0; i < 3; ++i) {
		AudioRoute r;
		runRoute(r, (RtAudio::Api)ordem[i], tokens[i], filtro);
		falaRota(r);
		rep.rotas.push_back(r);
	}

	int vivas = 0;
	for (const AudioRoute& r : rep.rotas)
		if (r.pediu && r.abriu.size() > 0 && r.callbacks > 0) ++vivas;
	rep.algumaRou = vivas > 0;

	if (rep.algumaRou) {
		for (const AudioRoute& r : rep.rotas) {
			if (r.abriu.empty() || r.callbacks == 0) continue;
			rep.resumo = r.api + " " + std::to_string(r.taxa) + "Hz "
				+ std::to_string(r.buffer) + "q " + r.formato + " "
				+ std::to_string((long long)(r.parede > 0 ? r.callbacks / r.parede : 0.0))
				+ "cb/s lat " + std::to_string((long long)r.latencia_ms) + "ms";
			break;
		}
		apLog("veredito: pelo menos uma rota de audio viva -> " + rep.resumo);
	} else {
		rep.resumo = "nenhuma rota abriu";
		apLog("VACUO: a rota nao abriu nenhum dispositivo com callback real"
			" | APIs na lib: " + rep.compilados + " | tentativas totais: "
			+ [&rep]() {
				int t = 0;
				for (const AudioRoute& r : rep.rotas) t += r.tentativas;
				return std::to_string(t);
			}());
	}
}

bool audioEmitAtiva() {
	char buf[8] = { 0 };
	size_t got = 0;
	return getenv_s(&got, buf, sizeof(buf), "CV0_AUDIOEMIT") == 0 && got > 0;
}

void audioEmitRun() {
	// sem a variavel este ponto nao existe: nenhum transporte e aberto e
	// nenhuma linha e escrita
	const std::string pedido = envStr("CV0_AUDIOEMIT");
	if (pedido.empty()) return;

	// o valor e a duracao. um texto que nao e numero nao e um pedido de 0 s, e
	// deixar o teste mudo seria uma mentira sobre a variavel: cai no default
	// de 5 s e diz isso na linha de abertura
	double segundos = std::atof(pedido.c_str());
	const bool naoEhNumero = (segundos == 0.0);
	if (naoEhNumero) segundos = 5.0;
	if (segundos < EMIT_MIN_S) segundos = EMIT_MIN_S;
	if (segundos > EMIT_MAX_S) segundos = EMIT_MAX_S;

	apLog("=== teste de emissao (CV0_AUDIOEMIT=" + pedido + ") | "
		+ std::to_string((long)segundos) + " s de " + std::to_string((long)EMIT_HZ)
		+ " Hz a " + std::to_string(EMIT_DBFS).substr(0, 5) + " dBFS"
		+ (naoEhNumero ? " | a variavel nao e um numero: duracao default" : ""));

	// ASIO primeiro, pela mesma razao da varredura: e a unica rota que nao
	// depende do servico que quebra. A primeira que abrir emite e para aqui.
	const int ordem[] = { (int)RtAudio::WINDOWS_ASIO, (int)RtAudio::WINDOWS_WASAPI,
		(int)RtAudio::WINDOWS_DS };
	const char* tokens[] = { "asio", "wasapi", "ds" };

	EmitState st;
	// o painel e uma segunda variavel, nao um modo do teste: sem ela o
	// comportamento e exatamente o que foi medido antes
	const std::string painel = envStr("CV0_AUDIOEMIT_PANEL");
	const bool querPainel = !painel.empty() && painel != "0" && painel != "off";
	if (querPainel)
		apLog("EMISSAO: CV0_AUDIOEMIT_PANEL pedido - a janela do driver vai abrir-se "
			"sobre o app e so um clique a fecha.");

	bool abriuAlguma = false;
	for (int i = 0; i < 3 && !abriuAlguma; ++i) {
		std::string motivo;
		abriuAlguma = emitRota((RtAudio::Api)ordem[i], tokens[i], segundos, EMIT_DBFS, st, motivo, querPainel);
		if (!abriuAlguma) apLog("EMISSAO: " + motivo);
	}

	// a frase que fecha o teste e a unica que nao pode faltar: tudo o que foi
	// impresso acima e o motorista a escrever num buffer. O veredito mora
	// fora deste processo e na boca de quem esta na sala.
	if (abriuAlguma)
		apLog("EMISSAO: fim da medicao. O que foi escrito esta medido; o que saiu "
			"da caixa nao pode ser medido por este codigo. So ha duas respostas "
			"possiveis e nenhuma delas e um numero: ouvi ou nao ouvi.");
	else
		apLog("EMISSAO: VACUO - nenhuma rota abriu e nenhuma amostra foi escrita. "
			"Sem emissao a pergunta continua a ser a da varredura, nao a do som.");
}
