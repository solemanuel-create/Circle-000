#include "midiProbe.h"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#pragma comment(lib, "winmm.lib")

namespace {

	// O escritor proprio, pelo mesmo motivo da sonda de audio: o log do OF nesta
	// build mata o processo na primeira escrita e o cv0Log do ofApp e local
	// aquele ficheiro.
	void mpLog(const std::string& s) {
		fprintf(stderr, "[midi] %s\n", s.c_str());
		fflush(stderr);
	}

	const int MSGS = 24;
	const double JANELA_MS = 1000.0;

	std::string errNome(unsigned int e) {
		switch (e) {
		case 0: return "sem erro";
		case 1: return "erro do driver";
		case 2: return "bad device id";
		case 3: return "nao habilitado";
		case 4: return "JA EM USO por outro app";
		case 5: return "handle invalido";
		case 6: return "sem driver";
		case 7: return "sem memoria";
		case 8: return "nao suportado";
		case 10: return "flag invalida";
		case 11: return "parametro invalido";
		case 12: return "handle ocupado";
		}
		return "codigo " + std::to_string(e);
	}

	std::string envStr(const char* name) {
		char buf[64] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), name) != 0 || got == 0) return std::string();
		return std::string(buf);
	}

	// O que a callback ve e um DWORD empacotado; desempacotar e o que permite
	// dizer na tela QUAL mensagem voltou, nao so quantas.
	std::string desempacota(unsigned int m) {
		char b[64];
		sprintf_s(b, "0x%02X %02X %02X", m & 0xFF, (m >> 8) & 0xFF, (m >> 16) & 0xFF);
		return std::string(b);
	}

	struct Ctx {
		std::atomic<long> recebidos{ 0 };
		std::atomic<long> casados{ 0 };
		std::atomic<long> outros{ 0 };
		std::atomic<long long> primeira{ -1 };
		std::atomic<unsigned int> bruto{ 0 };
		unsigned long long t0 = 0;
		const unsigned int* enviados = nullptr;
	};

	void CALLBACK inCb(HMIDIIN h, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR p1, DWORD_PTR p2) {
		(void)p2;
		(void)h;
		Ctx* c = reinterpret_cast<Ctx*>(dwInstance);
		if (wMsg == MIM_DATA && c->enviados) {
			// Em MIM_DATA o p1 E a mensagem empacotada, nao o header: as mensagens
			// curtas nao consomem buffer nenhum, e e por isso que a escuta de baixo
			// nao prepara SysEx.
			c->recebidos.fetch_add(1);
			const unsigned int m = (unsigned int)p1;
			for (int i = 0; i < MSGS; i++) {
				if (c->enviados[i] == m) { c->casados.fetch_add(1); break; }
			}
			long long antes = c->primeira.load();
			if (antes < 0) {
				c->primeira.store((long long)(GetTickCount64() - c->t0));
				// a primeira que voltou, guardada crua: saber que chegou algo sem
				// saber O QUE chegou e o suficiente para nao escrever nada util
				c->bruto.store(m);
			}
		} else if (wMsg == MIM_LONGDATA || wMsg == MIM_MOREDATA) {
			c->outros.fetch_add(1);
			// Nao rearma buffer nenhum: foi voltar a acrescentar dentro da callback
			// enquanto a thread principal fazia reset ao mesmo port que teve cara
			// de espera mutua no driver legado, e e o unico trabalho que aqui sobra.
		}
	}

	void coleta(std::vector<MidiPort>& v, bool saida) {
		const unsigned int n = saida ? midiOutGetNumDevs() : midiInGetNumDevs();
		for (unsigned int i = 0; i < n; i++) {
			MidiPort p;
			p.indice = (int)i;
			char nome[160] = { 0 };
			unsigned int r = 0;
			unsigned int tec = 0, sup = 0;
			if (saida) {
				MIDIOUTCAPSW caps;
				ZeroMemory(&caps, sizeof(caps));
				r = midiOutGetDevCapsW(i, &caps, sizeof(caps));
				if (r == 0) {
					WideCharToMultiByte(CP_UTF8, 0, caps.szPname, 32, nome, sizeof(nome) - 1, nullptr, nullptr);
					tec = caps.wTechnology; sup = caps.dwSupport;
				}
			} else {
				MIDIINCAPSW caps;
				ZeroMemory(&caps, sizeof(caps));
				r = midiInGetDevCapsW(i, &caps, sizeof(caps));
				if (r == 0) {
					WideCharToMultiByte(CP_UTF8, 0, caps.szPname, 32, nome, sizeof(nome) - 1, nullptr, nullptr);
					sup = caps.dwSupport;
				}
			}
			p.capsOk = (r == 0);
			p.retornoCaps = r;
			p.nome = p.capsOk ? std::string(nome) : ("(caps recusa: " + errNome(r) + ")");
			p.tecnologia = tec;
			p.suporte = sup;
			v.push_back(p);
		}
	}

	// UM cabo, das duas pontas. Liga a escuta, abre a saida, manda MSGS
	// mensagens, espera a janela e fecha sozinha cada chamada com a sua marca.
	//
	// Cada chamada e precedida por uma linha de etapa: o primeiro laco real nesta
	// maquina parou dentro de uma delas sem dizer qual, e sem rastro nao ha como
	// saber se foi o port, o driver ou a nossa sequencia. As etapas sao o que
	// transforma um bloqueio numa resposta.
	void laco(MidiLoop& L, unsigned int idxOut, unsigned int idxIn,
		const std::vector<unsigned int>& enviados) {

		Ctx c;
		c.enviados = enviados.data();
		c.t0 = GetTickCount64();

		HMIDIIN hi = nullptr;
		mpLog("  etapa: midiInOpen in=" + std::to_string(idxIn));
		unsigned int ri = midiInOpen(&hi, idxIn, (DWORD_PTR)&inCb, (DWORD_PTR)&c, CALLBACK_FUNCTION);
		L.abriuIn = (ri == 0);
		L.erroIn = ri;
		if (!L.abriuIn) { L.nota = "midiInOpen[" + std::to_string(idxIn) + "] = " + errNome(ri); return; }

		// Mensagem curta nao consome buffer de SysEx: o MIM_DATA traz o DWORD na
		// propria callback. Os 16 headers preparados da primeira versao foram o
		// unico trabalho que a gente deu ao driver alem do necessario, e foi na
		// devolucao deles que a maquina parou. Aqui so se liga a escuta.
		mpLog("  etapa: midiInStart");
		if (midiInStart(hi) != 0) L.nota = "midiInStart recusou";

		HMIDIOUT ho = nullptr;
		mpLog("  etapa: midiOutOpen out=" + std::to_string(idxOut));
		unsigned int ro = midiOutOpen(&ho, idxOut, 0, 0, CALLBACK_NULL);
		L.abriuOut = (ro == 0);
		L.erroOut = ro;

		if (L.abriuOut) {
			mpLog("  etapa: " + std::to_string(MSGS) + " midiOutShortMsg");
			for (int i = 0; i < MSGS; i++) {
				const unsigned int r = midiOutShortMsg(ho, enviados[i]);
				if (r == 0) L.enviados++;
				else if (i == 0) L.nota = "midiOutShortMsg recusou a primeira: " + errNome(r);
				Sleep(4);
			}
			mpLog("  etapa: espera da janela; ate aqui enviados " + std::to_string(L.enviados)
				+ " recebidos " + std::to_string((int)c.recebidos.load()));
			const unsigned long long fim = c.t0 + (unsigned long long)JANELA_MS;
			while (GetTickCount64() < fim) Sleep(10);
			L.recebidos = (int)c.recebidos.load();
			L.casados = (int)c.casados.load();
			L.outros = (int)c.outros.load();
			const long long p = c.primeira.load();
			L.ms_ate_primeira = p < 0 ? -1.0 : (double)p;
			L.ms_janela = (double)(GetTickCount64() - c.t0);
			// recebidos sem casamento e uma resposta diferente de vazio: e o caso
			// em que o cabo entrega, mas entrega outra coisa (um clock, uma nota de
			// outro app), e sem o hex na tela isso ficaria indistinguivel de mudo.
			if (L.recebidos > 0 && L.casados == 0 && L.nota.empty())
				L.nota = "voltou e nao casa: primeira " + desempacota(c.bruto.load());
			mpLog("  etapa: midiOutClose");
			midiOutClose(ho);
		}

		// Parar e fechar sao as duas chamadas que sobraram, e cada uma tem a sua
		// linha: o laco anterior morreu precisamente aqui dentro, e um bloqueio
		// sem relogio nao se diferencia de um driver lento sem esta marca.
		mpLog("  etapa: midiInStop");
		const unsigned int rs = midiInStop(hi);
		if (rs != 0 && L.nota.empty()) L.nota = "midiInStop = " + errNome(rs);
		mpLog("  etapa: midiInClose");
		const unsigned int rc = midiInClose(hi);
		if (rc != 0 && L.nota.empty()) L.nota = "midiInClose = " + errNome(rc);
	}

}

// ==================== porta publica ====================
bool midiProbeAtiva() {
	char buf[8] = { 0 };
	size_t got = 0;
	return getenv_s(&got, buf, sizeof(buf), "CV0_MIDIPROBE") == 0 && got > 0;
}

void midiProbeRun(MidiProbeReport& rep) {
	rep.ativo = midiProbeAtiva();
	if (!rep.ativo) return;   // inerte: nenhuma linha daqui roda sem a variavel

	const std::string par = envStr("CV0_MIDIPROBE_PAR");
	mpLog("=== sonda de MIDI (CV0_MIDIPROBE) winmm"
		+ (par.empty() ? "" : " | filtro de cabo: " + par));

	rep.saidas.reserve(8);
	rep.entradas.reserve(8);
	coleta(rep.saidas, true);
	coleta(rep.entradas, false);

	std::string s, e;
	for (const MidiPort& p : rep.saidas) { if (!s.empty()) s += " | "; s += "[" + std::to_string(p.indice) + "] " + p.nome; }
	for (const MidiPort& p : rep.entradas) { if (!e.empty()) e += " | "; e += "[" + std::to_string(p.indice) + "] " + p.nome; }
	mpLog("saidas " + std::to_string(rep.saidas.size()) + ": " + (s.empty() ? "nenhuma" : s));
	mpLog("entradas " + std::to_string(rep.entradas.size()) + ": " + (e.empty() ? "nenhuma" : e));

	// 24 mensagens: 12 notas ligadas e as mesmas 12 desligadas, com um CC no
	// meio. Nada aqui e musica - e uma assinatura que tem que voltar igual.
	std::vector<unsigned int> enviados;
	enviados.reserve(MSGS);
	for (int i = 0; i < 12; i++) enviados.push_back(0x90u | (0x30u + i) * 0x100u | 0x64u * 0x10000u);
	enviados.push_back(0xB0u | 0x07u * 0x100u | 0x60u * 0x10000u);
	enviados.push_back(0xB0u | 0x0Au * 0x100u | 0x40u * 0x10000u);
	for (int i = 0; i < 10; i++) enviados.push_back(0x80u | (0x30u + i) * 0x100u | 0x40u * 0x10000u);

	// parear por nome: o cabo que se escuta a si mesmo e o que prova entrega.
	// Uma porta sem par homonimo nao e falha - e uma porta que nao e laco.
	for (const MidiPort& o : rep.saidas) {
		if (!o.capsOk) continue;
		if (!par.empty() && o.nome.find(par) == std::string::npos) continue;
		const MidiPort* in = nullptr;
		for (const MidiPort& i : rep.entradas) {
			if (i.capsOk && i.nome == o.nome) { in = &i; break; }
		}
		MidiLoop L;
		L.nome = o.nome;
		L.idxOut = o.indice;
		if (!in) {
			L.idxIn = -1;
			L.nota = "nenhuma entrada com o mesmo nome: nao e um cabo de laco";
			rep.lacos.push_back(L);
			mpLog("laco \"" + L.nome + "\": pulado - " + L.nota);
			continue;
		}
		L.idxIn = in->indice;
		laco(L, (unsigned int)L.idxOut, (unsigned int)L.idxIn, enviados);
		rep.lacos.push_back(L);

		if (!L.abriuIn || !L.abriuOut) {
			mpLog("laco \"" + L.nome + "\": NAO ABRIU out=" + (L.abriuOut ? "ok" : errNome(L.erroOut))
				+ " in=" + (L.abriuIn ? "ok" : errNome(L.erroIn))
				+ (L.nota.empty() ? "" : " | " + L.nota));
		} else if (L.casados > 0) {
			rep.lacosVivos++;
			mpLog("laco \"" + L.nome + "\": FECHADO - enviados " + std::to_string(L.enviados)
				+ " recebidos " + std::to_string(L.recebidos) + " casados " + std::to_string(L.casados)
				+ " | primeira em " + std::to_string((long long)L.ms_ate_primeira) + " ms"
				+ " | janela " + std::to_string((long long)L.ms_janela) + " ms"
				+ " | outras msgs " + std::to_string(L.outros)
				+ (L.nota.empty() ? "" : " | " + L.nota));
		} else {
			mpLog("laco \"" + L.nome + "\": VACUO - abriu as duas pontas, mandou "
				+ std::to_string(L.enviados) + ", casou " + std::to_string(L.casados)
				+ " de " + std::to_string(L.recebidos) + " recebidos em "
				+ std::to_string((long long)L.ms_janela) + " ms"
				+ (L.nota.empty() ? "" : " | " + L.nota));
		}
	}

	if (rep.lacosVivos > 0) {
		for (const MidiLoop& L : rep.lacos) {
			if (L.casados <= 0) continue;
			rep.resumo = L.nome + ": laco fechado " + std::to_string(L.casados) + "/"
				+ std::to_string(L.enviados) + " em " + std::to_string((long long)L.ms_ate_primeira) + " ms";
			break;
		}
		mpLog("veredito: MIDI entrega - " + rep.resumo);
	} else {
		rep.resumo = "nenhum laco devolveu mensagem";
		mpLog("VACUO: nenhuma porta devolveu o que foi enviado"
			" | saidas " + std::to_string(rep.saidas.size())
			+ " entradas " + std::to_string(rep.entradas.size()));
	}
}
