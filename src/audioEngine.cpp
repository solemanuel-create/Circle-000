#include "audioEngine.h"
#include "syncProbe.h"
#include "RtAudio.h"

#include <direct.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ==================== o que cada sinal faz ao som ====================
// A tabela esta escrita em duas partes e as duas tem de ser lidas juntas,
// porque o fonte e o que fica e o comentario e o que ele quer dizer:
//
//   ambiente   nivel do colchao.        A sala cheia e quieta da um tapete
//                                       continuo; a sala vazia da silencio. E
//                                       o unico sinal lento o bastante para
//                                       isto nao pulsar com o movimento.
//   coerencia  intervalo do colchao.    A quinta justa (3:2) quando as zonas
//                                       concordam, o tritono (raiz de 2)
//                                       quando estao partidas. Discordancia
//                                       coletiva e uma diferenca de harmonia,
//                                       nao de volume, e aqui ela e ouvida.
//   zona[6]    seis vozes.             A coluna da grade da o grau (triada
//                                       aberta), a linha da oitava (em cima =
//                                       agudo), a coluna da o pan. A grade
//                                       3x2 do rastreio vira um teclado de
//                                       seis lugares que o publico toca
//                                       ocupando o espaco.
//   energia    tamanho do conjunto.     Multiplica as vozes: mais gente
//                                       visivel, mais forte, sem mudar o que
//                                       se esta a ouvir.
//   densidade  brilho.                  Passa-baixo de 180 Hz a 6 kHz em
//                                       oitavas, nao em recta.
//   velocidade ar.                     Nivel e taxa de um tremolo sobre
//                                       ruido filtrado: uma sala quieta nao
//                                       granula, uma sala a mover-se granula
//                                       mais rapido, e nada aqui sobe o
//                                       volume so porque alguem agitou o
//                                       braco.
//   corpo que entrou                   Um ping por corpo, na nota que a
//                                       posicao dele pede. A idade que o
//                                       dispara e a MESMA do cabo MIDI, vinda
//                                       do chamador: o que a sala ouve ao
//                                       entrar alguem tem de ser o mesmo
//                                       frame que o TouchDesigner recebe.
//   rMaster    travao global.          O mesmo numero que o render usou neste
//                                       frame. O `g` corta as duas coisas e o
//                                       `n`/`m` sobe as duas, porque em cena
//                                       ninguem desliga a reacao de um dos
//                                       lados.
//
// Nada disto e uma medida, excepto a idade do pulso e o teto em dBFS. E por
// isso que o passo de entrega pergunta a ele se o mapa e o que ele quer, em
// vez de assumir que uma escolha de orelha se provou por numeros.

namespace {

	// O escritor proprio, pelo mesmo motivo das sondas: a escrita do log do OF
	// derruba esta build, e um motor que nao abriu tem de dizer isso num lugar
	// que sobreviva a ele proprio.
	void aeLog(const std::string& s) {
		fprintf(stderr, "[audio-eng] %s\n", s.c_str());
		fflush(stderr);
	}

	std::string envStr(const char* name) {
		char buf[64] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), name) != 0 || got == 0) return std::string();
		return std::string(buf);
	}

	const double AE_PI = 3.14159265358979323846;
	const double AE_SQRT2 = 1.41421356237309504880;

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

	// ==================== pecas do motor ====================

	// O instantaneo do barramento. Nao e um apontador para o CvSignals: a
	// thread do motorista nao pode ler um objeto que a main thread esta a
	// escrever a 30 Hz. O que passa por aqui sao doze numeros ja passados por
	// atomicos, e o motivo pelo qual um deles pode vir do frame anterior
	// enquanto outro vem deste nao ofende nada: todos eles sao continuos no
	// tempo porque o envelope que os produziu e exponencial. Um audio leria o
	// mesmo desalinhamento como uma mudanca de um milissegundo.
	struct Snap {
		float energia = 0.0f;
		float densidade = 0.0f;
		float velocidade = 0.0f;
		float coerencia = 0.0f;
		float ambiente = 0.0f;
		float master = 1.0f;
		float zona[CV_ZONES] = { 0.0f };
		bool activo = true;
	};

	// Um evento de "entrou alguem", produzido na main e consumido pelo
	// motorista. Os tres campos vao em bytes porque a fila e um anel de
	// copiaveis triviais e um `float` a meio de uma troca seria um problema a
	// menos para pensar.
	//
	// O `nasceUs` nao muda nada do que toca: e o carimbo do nascimento a viajar
	// dentro do Evento para a thread do motorista poder subtrair-lhe a propria
	// hora. Sem a sonda armada fica zero, e zero e o valor que a sonda le como
	// "desarmada" - o anel nao e tocado, o sintetizador nem olha para ele.
	struct Evento {
		unsigned char nota = 60;
		unsigned char vel = 100;
		unsigned char pan = 128;
		long long nasceUs = 0;
	};

	struct Pulso {
		double fase = 0.0, inc = 0.0;
		double fase2 = 0.0, inc2 = 0.0;
		float amp = 0.0;
		float gl = 1.0f, gr = 1.0f;
	};

	// Todo o estado do motor. Vive nas maos da thread do motorista e de mais
	// ninguem: nem a main thread nem o HUD o leem enquanto ele trabalha, os
	// resultados sobem para os atomicos do contexto no fim de cada bloco.
	struct Sint {
		double df[2] = { 0.0, 0.0 };
		double di[2] = { 0.0, 0.0 };
		double zf[CV_ZONES] = { 0.0 };
		double zi[CV_ZONES] = { 0.0 };
		double zf2[CV_ZONES] = { 0.0 };
		double zi2[CV_ZONES] = { 0.0 };
		double tf = 0.0, ti = 0.0;
		Pulso pulso[CV_AUDIO_PING_VOZES];
		int prox = 0;

		double lpL = 0.0, lpR = 0.0, lpg = 0.0;
		double arLp = 0.0, arLpg = 0.0;
		float fade = 0.0f;
		float autoGain = 1.0f;
		unsigned int semente = 0x9E3779B9u;

		// medicao DO BLOCO QUE ACABOU DE SAIR, nao acumulada: o contexto soma
		// estes tres numeros para os atomicos a cada callback, e um contador
		// que se repassa por inteiro depois de se somar a si proprio conta o
		// mesmo ceifado duas vezes, tres, quatro.
		double pico = 0.0;
		long ceifado = 0;
		int vozesVivas = 0;
	};

	// ==================== osciladores ====================
	// Um acumulador de fase por oscilador, com incremento por amostra, e o
	// mesmo truque do teste de emissao: a afinacao nao depende de somar
	// muitos termos e por isso nao deriva. A fase vai a 1,0 e volta a 0,0.
	inline float osc(double& fase, double inc) {
		fase += inc;
		if (fase >= 1.0) fase -= 1.0;
		return sinf((float)(2.0 * AE_PI * fase));
	}

	inline double hzNota(int midi) {
		return 440.0 * std::pow(2.0, ((double)midi - 69.0) / 12.0);
	}

	// xorshift32: deterministico, sem tabela, e o ruido que o ar precisa. Um
	// generator de verdade nao cabe num callback e o rand() da libc aloca e
	// pega num lock, que e justamente o que nao se pode fazer aqui.
	inline float ruido(unsigned int& s) {
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		return (float)((double)(s >> 8) / 8388608.0 - 1.0);
	}

	// Pan de igual potencia: p=0 e a esquerda, p=0,5 e o centro. Duas colunas
	// vizinhas com o mesmo nivel dao a mesma largura que uma sozinha no centro.
	inline void pan(float p, float& gl, float& gr) {
		const double a = (double)p * AE_PI * 0.5;
		gl = (float)cos(a);
		gr = (float)sin(a);
	}

	inline float clipa(float x, float lim, long& ceifado) {
		if (x > lim) { ceifado++; return lim; }
		if (x < -lim) { ceifado++; return -lim; }
		return x;
	}

	// ==================== o motor puro ====================
	// Enche `out` com `nFrames` quadros intercalados e devolve nada. E uma
	// funcao livre e nao um metodo por um motivo de prova: o teste offline
	// chama-a com um instantaneo inventado e o palco chama-a com o instantaneo
	// publicado pela main thread. As duas ouvem o mesmo codigo, e por isso a
	// medicao do teste vale para o que sai da caixa - o que nao aconteceria se
	// o teste fosse uma copia da formula.
	void sintetiza(float* out, unsigned nFrames, int canais, double taxa,
		const Snap& s, Sint& st, const Evento* ev, int nEv, double teto) {

		if (!out || nFrames == 0 || canais <= 0) return;

		// ---------- uma vez por bloco ----------
		// O colchao tem a fundamental sempre e um segundo oscilador cuja RAZAO
		// desliza entre a quinta justa e o tritono. Deslizar a frequencia, e
		// nao fazer crossfade entre duas notas, e o que elimina o clique: nao
		// ha dois pontos de fase a casar, ha uma linha continua.
		const double baseDrone = 110.0;
		const double razao = 1.5 * (double)s.coerencia
			+ AE_SQRT2 * (1.0 - (double)s.coerencia);
		st.di[0] = baseDrone / taxa;
		st.di[1] = baseDrone * razao / taxa;
		// Sem piso: a promessa da tabela e "a sala vazia da silencio", e um
		// 0,15 escondido aqui seria um colchao a tocar sozinho entre duas
		// pessoas - o zumbido que o teste de sala vazia existe para acusar.
		const float gDrone = CV_AUDIO_G_DRONE * s.ambiente;

		// As vozes da grade: coluna da o grau, linha da oitava, com a linha de
		// cima a ser a oitava de cima - o mesmo espelho vertical que a tela ja
		// usa, para "em cima e mais agudo" significar a mesma coisa nos dois
		// sentidos.
		for (int z = 0; z < CV_ZONES; z++) {
			const int col = z % CV_ZONE_COLS;
			const int row = z / CV_ZONE_COLS;
			const int sem = CV_AUDIO_GRAUS[col] + 12 * (CV_ZONE_ROWS - 1 - row);
			st.zi[z] = hzNota(CV_AUDIO_BASE_NOTA + sem) / taxa;
			st.zi2[z] = 2.0 * st.zi[z];
		}
		float zgl[CV_ZONES], zgr[CV_ZONES];
		for (int z = 0; z < CV_ZONES; z++) {
			const float p = (float)((z % CV_ZONE_COLS) + 0.5) / (float)CV_ZONE_COLS;
			pan(p, zgl[z], zgr[z]);
		}

		// A promessa da tabela: energia e o tamanho do conjunto, e ela mexe na
		// forca das vozes sem mexer no que elas sao. O 0,3 em baixo e para uma
		// zona ocupada por uma pessoa nao ser inaudivel - o que se quer ouvir
		// e a diferenca entre uma e dez, nao o zero de uma.
		const float gZona = CV_AUDIO_G_ZONA * (0.3f + 0.7f * s.energia);

		// O brilho obedece a area, em oitavas e nao em recta: o que se quer e
		// que o dobro de corpo suba uma oitava de corte, nao metade da corrida
		// toda. 180 Hz com um dedo no palco, 6 kHz com a sala inteira dentro.
		const double fc = 180.0 * std::pow(33.0, (double)s.densidade);
		st.lpg = 1.0 - exp(-2.0 * AE_PI * fc / taxa);

		// O ar: um passa-alto fixo sobre ruido, moldado pela velocidade. A
		// taxa do tremolo e o que distingue sala quieta de sala a mexer-se.
		const float gAr = CV_AUDIO_G_AR * s.velocidade;
		st.ti = (1.5 + 11.0 * (double)s.velocidade) / taxa;
		st.arLpg = 1.0 - exp(-2.0 * AE_PI * 1500.0 / taxa);

		// O decaimento por amostra dos pulsos, uma conta por bloco.
		const float dec = (float)exp(-1.0 / (taxa * CV_AUDIO_PING_TAU_S));

		// Os eventos do frame: cada um rouba a voz mais antiga e comeca a
		// fase em zero. Comecar a fase onde ela estava seria um degrau de
		// amplitude no ataque, que e o clique que a rampa veio evitar.
		for (int e = 0; e < nEv; e++) {
			Pulso& p = st.pulso[st.prox];
			st.prox = (st.prox + 1) % CV_AUDIO_PING_VOZES;
			const double hz = hzNota((int)ev[e].nota);
			p.inc = hz / taxa;
			p.inc2 = 2.0 * p.inc;
			p.fase = 0.0;
			p.fase2 = 0.0;
			p.amp = (float)ev[e].vel / 127.0f;
			pan((float)ev[e].pan / 255.0f, p.gl, p.gr);
		}

		// A rampa do liga/desliga anda por amostra, nao por bloco: um passo de
		// 10,7 ms em cima de uma forma de onda ja feita e um degrau de ganho,
		// e um degrau de ganho ouve-se.
		const float alvo = s.activo ? 1.0f : 0.0f;
		const float f0 = st.fade;
		float f1 = st.fade;
		const float fpasso = (float)(nFrames / (taxa * CV_AUDIO_FADE_S));
		if (f1 < alvo) { f1 += fpasso; if (f1 > alvo) f1 = alvo; }
		else if (f1 > alvo) { f1 -= fpasso; if (f1 < alvo) f1 = alvo; }
		const float fInc = (f1 - f0) / (float)nFrames;
		st.fade = f1;
		// O teto entra uma so vez por amostra, e e tambem o ponto de clip: uma
		// conta cujo resultado nao pode passar o proprio limite.
		const float tetoF = (float)teto;
		const float ga = st.autoGain;
		const float m = s.master;

		// ---------- por amostra ----------
		float fv = f0;
		double picoAqui = 0.0;
		long ceifAqui = 0;
		for (unsigned i = 0; i < nFrames; i++) {
			fv += fInc;

			float xl = gDrone * (0.6f * osc(st.df[0], st.di[0])
				+ 0.4f * osc(st.df[1], st.di[1]));
			float xr = xl;

			// As fases das vozes avancam sempre, mesmo com a zona calada.
			// Congelar a fase de quem esta mudo faria a nota recomecar a meio
			// de um periodo quando ela voltasse.
			for (int z = 0; z < CV_ZONES; z++) {
				const float v = gZona * s.zona[z]
					* (osc(st.zf[z], st.zi[z]) + 0.3f * osc(st.zf2[z], st.zi2[z]));
				xl += v * zgl[z];
				xr += v * zgr[z];
			}

			for (int v = 0; v < CV_AUDIO_PING_VOZES; v++) {
				Pulso& p = st.pulso[v];
				if (p.amp <= 1e-5f) continue;
				const float x = p.amp * CV_AUDIO_G_PING
					* (osc(p.fase, p.inc) + 0.3f * osc(p.fase2, p.inc2));
				xl += x * p.gl;
				xr += x * p.gr;
				p.amp *= dec;
			}

			const float rn = ruido(st.semente);
			st.arLp += st.arLpg * ((double)rn - st.arLp);
			const float ar = (rn - (float)st.arLp) * gAr
				* (0.5f + 0.5f * osc(st.tf, st.ti));
			xl += ar;
			xr += ar;

			// Um polo, por canal. Filtrar depois do pan e identico a filtrar
			// cada voz antes dela: a conta e linear e os coeficientes sao os
			// mesmos, e aqui ela custa dois acumuladores em vez de doze.
			st.lpL += st.lpg * ((double)xl * (double)m - st.lpL);
			st.lpR += st.lpg * ((double)xr * (double)m - st.lpR);

			const float g = fv * ga * CV_AUDIO_ESCALA;
			// O travao e o teto, nao o 0 dBFS da placa: a promessa desta peca e
			// "nunca sai da caixa mais alto do que o nivel que ele aprovou", e um
			// guardiao posto em 1,0 nunca a cumpraria - a referencia medida da
			// sala cheia da em master 2 cerca de 0,37, ou seja, o clip de 0 dBFS
			// era codigo morto e o limiter um enfeite que nunca se via.
			const float L = clipa((float)st.lpL * g * tetoF, tetoF, ceifAqui);
			const float R = clipa((float)st.lpR * g * tetoF, tetoF, ceifAqui);
			out[(size_t)i * (size_t)canais] = L;
			if (canais > 1) out[(size_t)i * (size_t)canais + 1] = R;

			const double aL = L < 0.0f ? -(double)L : (double)L;
			const double aR = R < 0.0f ? -(double)R : (double)R;
			if (aL > picoAqui) picoAqui = aL;
			if (aR > picoAqui) picoAqui = aR;
		}

		// ---------- fim do bloco ----------
		// O travao com memoria: um bloco que ceifou perde 10% do ganho ja no
		// bloco seguinte, e um bloco limpo recupera 2% do que falta, o que da
		// cerca de um segundo e meio para voltar ao lugar sem que a sala ouca a
		// subida. O numero aparece no HUD: um limiter que nao se anuncia e uma
		// mudanca de volume que ninguem pediu.
		if (ceifAqui > 0) st.autoGain *= 0.9f;
		else st.autoGain += (1.0f - st.autoGain) * 0.02f;
		if (st.autoGain < 0.05f) st.autoGain = 0.05f;
		if (st.autoGain > 1.0f) st.autoGain = 1.0f;

		st.pico = picoAqui;
		st.ceifado = ceifAqui;
		int vivas = 0;
		for (int v = 0; v < CV_AUDIO_PING_VOZES; v++)
			if (st.pulso[v].amp > 1e-5f) vivas++;
		st.vozesVivas = vivas;
	}

	// ==================== WAV ====================
	// 16 bits com conteudo, PCM, com o cabecalho escrito a mao: um WAV de
	// virgula flutuante nao abre em metade dos leitores, e o que se quer do
	// ficheiro e que o Python, o Audacity e o ouvido dele o abram todos. A
	// quantizacao e so da copia gravada - a saida viva continua em float32.
	void put16(unsigned char* h, int off, unsigned int v) {
		h[off] = (unsigned char)(v & 0xFF);
		h[off + 1] = (unsigned char)((v >> 8) & 0xFF);
	}
	void put32(unsigned char* h, int off, unsigned int v) {
		h[off] = (unsigned char)(v & 0xFF);
		h[off + 1] = (unsigned char)((v >> 8) & 0xFF);
		h[off + 2] = (unsigned char)((v >> 16) & 0xFF);
		h[off + 3] = (unsigned char)((v >> 24) & 0xFF);
	}

	bool escreveWav(const std::string& caminho, const float* dados, long quadros,
		int canais, int taxa, std::string& erro, double* picoFora, long* ceifFora) {

		FILE* f = nullptr;
		if (fopen_s(&f, caminho.c_str(), "wb") != 0 || !f) {
			erro = "fopen recusou " + caminho;
			return false;
		}
		const unsigned int dadosBytes = (unsigned int)(quadros * canais * 2);
		unsigned char h[44];
		memset(h, 0, sizeof(h));
		memcpy(h, "RIFF", 4);
		put32(h, 4, dadosBytes + 36);
		memcpy(h + 8, "WAVE", 4);
		memcpy(h + 12, "fmt ", 4);
		put32(h, 16, 16);
		put16(h, 20, 1);					// PCM
		put16(h, 22, (unsigned int)canais);
		put32(h, 24, (unsigned int)taxa);
		put32(h, 28, (unsigned int)taxa * (unsigned int)canais * 2u);
		put16(h, 32, (unsigned int)canais * 2u);
		put16(h, 34, 16);					// bits por amostra
		memcpy(h + 36, "data", 4);
		put32(h, 40, dadosBytes);
		if (fwrite(h, 1, 44, f) != 44) { erro = "cabecalho nao escreveu"; fclose(f); return false; }

		double pico = 0.0;
		long ceif = 0;
		const long total = quadros * canais;
		std::vector<short> pcm((size_t)total);
		for (long i = 0; i < total; i++) {
			float x = dados[i];
			if (x > 1.0f) { x = 1.0f; ceif++; }
			if (x < -1.0f) { x = -1.0f; ceif++; }
			const double a = x < 0.0 ? -x : x;
			if (a > pico) pico = a;
			pcm[(size_t)i] = (short)(x * 32767.0f);
		}
		const bool ok = fwrite(pcm.data(), sizeof(short), (size_t)total, f) == (size_t)total;
		fclose(f);
		if (!ok) erro = "dados nao escreveram";
		if (picoFora) *picoFora = pico;
		if (ceifFora) *ceifFora = ceif;
		return ok;
	}

	std::string caminhoAbsoluto(const std::string& nome) {
		char dir[512] = { 0 };
		if (!_getcwd(dir, sizeof(dir))) return nome;
		std::string p(dir);
		if (!p.empty() && p[p.size() - 1] != '\\' && p[p.size() - 1] != '/') p += "\\";
		return p + nome;
	}

	// ==================== teste offline ====================
	// O motor sem dispositivo nenhum: a mesma funcao de sintese, instantaneos
	// inventados um de cada vez, e o resultado gravado para se medir com
	// outra linguagem. Existe porque uma saida de audio so tem uma prova
	// natural - o ouvido - e essa nao esta disponivel enquanto ele nao estiver
	// na sala; e existe tambem porque as coisas que este teste mede (escala,
	// afinacao, rampa, limiter, silencio de sala vazia) nao se ouvem uma por
	// uma num run de palco.
	struct Cena {
		const char* nome;
		double seg;
		Snap s;
		int pulsos;			// 1 = lancar um pulso a cada terco de segundo
		bool cortaA_meio;	// a cena comeca ligada e desliga-se a meio
	};

	Cena novaCena(const char* nome, double seg) {
		Cena c;
		c.nome = nome;
		c.seg = seg;
		c.pulsos = 0;
		c.cortaA_meio = false;
		c.s.activo = true;
		return c;
	}

	// Uma cena: blocos de 512, o instantaneo fixo dela, e no fim a frase que a
	// descreve por numeros. O `taxa` vai por valor porque nenhuma cena muda a
	// taxa, e uma cena que a mudasse estaria a estragar a comparacao com a
	// vizinha.
	//
	// Nao ha memoria partilhada entre cenas, e houve: o salto passou a medir-se
	// dentro de cada canal e de cada cena, com o primeiro quadro dela de fora.
	// Uma cena muda de instantaneo por decreto, o que em palco nunca acontece
	// porque la quem muda e o envelope que seguiu o alvo; contar a virada entre
	// duas cenas seria chamar clique a uma ordem da propria medicao, e contar a
	// amostra do outro canal do mesmo instante seria medir largura esterica.
	void cenaCorre(const Cena& c, std::vector<float>& wav, long taxa, int canais,
		double teto, Sint& st, int& seq) {

		const unsigned nF = CV_AUDIO_BUFFER;
		const long blocos = (long)(c.seg * (double)taxa / (double)nF);
		// um pulso por terco de segundo: e o ritmo com que uma pessoa entra num
		// palco, e e o que a fila de eventos tem de suportar sem perder nada
		const long cada = (long)((double)taxa / 3.0 / (double)nF);
		if (cada < 1) return;
		std::vector<float> bloco((size_t)nF * (size_t)canais);

		Snap s = c.s;
		double pico = 0.0;
		long ceif = 0;
		long ceifTeto = 0;
		double saltoA = 0.0, saltoB = 0.0;
		double somaQ = 0.0;
		long n = 0;
		// O salto compara o quadro anterior DO MESMO CANAL. Comparar a amostra
		// anterior da faixa toda - que e o que aqui estave - compara L com R do
		// mesmo instante, e numa saida com uma cena ceifada no tecto e as duas
		// pontas do pan em sentidos opostos isso da 2 x teto: um numero de
		// largura esterica vestido de deteccao de clique. A analise por fora deu
		// 0,00276 para o mesmo salto que aqui saiu 0,03576, e foi assim que se
		// viu.
		float anterior[2] = { 0.0f, 0.0f };
		// as tres perguntas que um numero so nao separa, na cena do corte:
		// `quando` aconteceu o maior salto da segunda metade (a inclinacao
		// natural de uma onda ainda cheia vale quase tanto quanto um clique, e
		// cai logo a seguir ao corte); `se caiu na fronteira de um bloco`, que e
		// o unico sitio onde o ganho do limiter se mexe e portanto onde ele pode
		// partir a onda; e `o que mede a cauda`, a parte em que a rampa ja
		// acabou e o valor tem de ser exactamente zero.
		double tSaltoB = -1.0;
		double saltoFronteiraB = 0.0;
		double saltoCaudaB = 0.0;
		// a cauda comeca uma janela depois de a rampa acabar, para a medicao nao
		// apanhar o ultimo passo dela, que e legitimo
		const double tCauda = c.seg * 0.5 + CV_AUDIO_FADE_S + (double)nF / (double)taxa + 0.01;

		for (long b = 0; b < blocos; b++) {
			const double t = (double)(b * nF) / (double)taxa;
			Evento ev[1];
			int nEv = 0;
			if (c.pulsos > 0 && (b % cada) == 0) {
				// a nota varre a largura do quadro em passos de sete, para a
				// cena nao tocar sempre a mesma nota e o teste nao provar so
				// uma posicao da escala
				const float cx = 0.1f + 0.8f * (float)((seq++) % 7) / 6.0f;
				ev[0].nota = (unsigned char)(CV_AUDIO_BASE_NOTA
					+ CV_AUDIO_PENT[(int)(cx * 4.99f)] + (cx < 0.5f ? 12 : 0));
				ev[0].vel = 100;
				ev[0].pan = (unsigned char)(cx * 255.0f);
				nEv = 1;
			}
			s.activo = c.cortaA_meio ? (t < c.seg * 0.5) : c.s.activo;
			sintetiza(bloco.data(), nF, canais, (double)taxa, s, st, ev, nEv, teto);
			// a conta do propria motor: quantas amostras pediram mais do que o
			// teto. E ela, e nao uma re-medicao minha, que o limiter ve e de que
			// ele faz depender o ganho.
			ceifTeto += st.ceifado;

			for (unsigned i = 0; i < nF; i++) {
				for (int ch = 0; ch < canais; ch++) {
					const float v = bloco[(size_t)i * (size_t)canais + (size_t)ch];
					// O primeiro bloco da cena nao entra em conta nenhuma. Ele e
					// o assentamento do filtro, que ainda carrega a cena
					// anterior: medido, o pico e o rms de uma cena de ruido
					// baixo saem dez vezes acima do que ela toca, porque o que
					// se esta a ver e a cauda do acorde anterior a morrer em
					// cerca de um milissegundo. Em palco isto nao existe, porque
					// la quem muda e o envelope, devagar.
					if (b > 0) {
						const double a = v < 0.0f ? -(double)v : (double)v;
						if (a > pico) pico = a;
						if (a > 1.0) ceif++;
						if (ch < 2) {
							const double d = (double)v - (double)anterior[ch];
							const double ad = d < 0.0 ? -d : d;
							if (t < c.seg * 0.5) { if (ad > saltoA) saltoA = ad; }
							else {
								if (ad > saltoB) { saltoB = ad; tSaltoB = t; }
								if (i == 0 && ch == 0 && ad > saltoFronteiraB)
									saltoFronteiraB = ad;
								if (t > tCauda && ad > saltoCaudaB) saltoCaudaB = ad;
							}
						}
						somaQ += (double)v * (double)v;
						n++;
					}
					if (ch < 2) anterior[ch] = v;
				}
			}
			wav.insert(wav.end(), bloco.begin(), bloco.end());
		}

		// a cena fala por si no mesmo instante em que acaba, porque uma tabela
		// que se monta no fim nao diz qual das cenas partiu. As duas colunas de
		// ceifados sao perguntas diferentes: `fora do teto` e o que o limiter
		// viu e come; `acima de 0` e o que passou a copia inteira e so pode
		// valer zero - e se nao valer, o clip do teto nao esta a segurar nada.
		// O salto vem nas duas metades: na cena da rampa, a segunda metade e a
		// do corte, e e la que um clique existiria.
		fprintf(stderr, "[audio-eng]   TESTE %-24s %5.1f s | pico %.4f (%.1f dBFS)"
			" | rms %.4f | fora do teto %ld | acima de 0 %ld | salto %.5f/%.5f\n",
			c.nome, c.seg, pico,
			pico > 1e-9 ? 20.0 * log10(pico) : -99.0,
			n > 0 ? sqrt(somaQ / (double)n) : 0.0, ceifTeto, ceif, saltoA, saltoB);
		// a cena do corte e a unica com alguma coisa para provar sobre a rampa, e
		// por isso e a unica que da as coordenadas do salto. O veredicto cabe nos
		// tres numeros: se a cauda nao for zero, a rampa nao chegou ao zero e o
		// que se ouve entre cenas e o motor; se o maior salto cair na fronteira
		// de um bloco, foi o ganho do limiter que o fez; se cair logo a seguir
		// ao corte e do tamanho do salto da primeira metade, e a inclinacao da
		// propria onda a passar por ali e nao ha clique nenhum.
		if (c.cortaA_meio) {
			fprintf(stderr, "[audio-eng]     rampa | maior salto da 2a metade %.5f"
				" em t=%.4f s (corte em %.4f) | na fronteira de bloco %.5f"
				" | cauda depois de %.3f s %.5f | ga no fim %.4f\n",
				saltoB, tSaltoB, c.seg * 0.5, saltoFronteiraB, tCauda,
				saltoCaudaB, st.autoGain);
		}
		fflush(stderr);
	}

	void testeOffline(const std::string& pedido) {
		double segPedida = atof(pedido.c_str());
		if (segPedida <= 0.0) segPedida = 1.0;
		if (segPedida > 4.0) segPedida = 4.0;

		const int canais = 2;
		const long taxa = 48000;
		const double teto = pow(10.0, CV_AUDIO_TETO_DBFS / 20.0);

		aeLog("=== teste offline (mesma funcao do palco, sem dispositivo) | teto "
			+ std::to_string(CV_AUDIO_TETO_DBFS).substr(0, 5) + " dBFS = "
			+ std::to_string(teto).substr(0, 6) + " | " + std::to_string(segPedida).substr(0, 4)
			+ " s por cena ===");

		std::vector<float> wav;
		Sint st;
		int seq = 0;

		// 1. sala vazia. Se isto nao der silencio, o que se ouve em intervalo
		// longo nao e a peca, e o motor a zumbir.
		Cena vazio = novaCena("sala vazia", segPedida);
		cenaCorre(vazio, wav, taxa, canais, teto, st, seq);

		// 2 e 3. o colchao nas duas pontas da coerencia: a quinta justa e o
		// tritono. As duas frequencias esperadas vao impressas para a analise
		// poder ir la procura-las, em vez de confiar que a conta foi feita.
		Cena dQ = novaCena("colchao quinta (coer=1)", segPedida);
		dQ.s.ambiente = 1.0f; dQ.s.coerencia = 1.0f;
		cenaCorre(dQ, wav, taxa, canais, teto, st, seq);
		aeLog("  esperado: 110,0 Hz e 165,0 Hz");

		Cena dT = novaCena("colchao tritono (coer=0)", segPedida);
		dT.s.ambiente = 1.0f; dT.s.coerencia = 0.0f;
		cenaCorre(dT, wav, taxa, canais, teto, st, seq);
		aeLog("  esperado: 110,0 Hz e 155,6 Hz");

		// 4. uma zona so, e depois as seis: a diferenca de pico entre as duas e
		// o que diz se os ganhos nominais estao a encher a caixa ou a deixa-la
		// vazia.
		Cena z1 = novaCena("zona 0 so (col 0 row 0)", segPedida);
		z1.s.energia = 0.6f; z1.s.coerencia = 1.0f; z1.s.zona[0] = 1.0f;
		cenaCorre(z1, wav, taxa, canais, teto, st, seq);
		// o `esperado` desta linha esteve errado e a analise por fora apanhou-o:
		// a zona 0 e a linha de CIMA, que e a oitava de cima, pelo que e um La 4
		// (69 -> 440 Hz) e nao um La 3. O que se mede no ficheiro da 440 com a
		// fundamental presente e a 220 ausente, que e o espelho vertical que a
		// tela ja usa - a tabela de cima nao mentiu, so a frase abaixo dela.
		aeLog("  esperado: 440,0 Hz (A4 = nota 69: coluna 0 grau 0, linha de cima +12)");

		Cena z6 = novaCena("as seis zonas", segPedida);
		z6.s.energia = 1.0f; z6.s.coerencia = 1.0f; z6.s.ambiente = 1.0f;
		for (int z = 0; z < CV_ZONES; z++) z6.s.zona[z] = 1.0f;
		cenaCorre(z6, wav, taxa, canais, teto, st, seq);
		aeLog("  esperado: 220 e 261,6 e 329,6 na linha de baixo, 440 e 523,2 e"
			" 659,2 na de cima, mais o colchao em 110 e 165");

		// 5. brilho: o mesmo sinal com o filtro no chao e no teto. A passa-baixo
		// de 180 Hz come o acorde de 220 a 659 quase todo, e a de 6 kHz nao
		// come nada - o que se espera e uma diferenca de rms grande e de pico
		// pequena, porque o pico fica na fundamental.
		Cena escuro = novaCena("densidade 0 (180 Hz)", segPedida);
		escuro.s.energia = 1.0f; escuro.s.coerencia = 1.0f; escuro.s.ambiente = 1.0f;
		for (int z = 0; z < CV_ZONES; z++) escuro.s.zona[z] = 0.8f;
		escuro.s.densidade = 0.0f;
		cenaCorre(escuro, wav, taxa, canais, teto, st, seq);

		Cena claro = escuro;
		claro.nome = "densidade 1 (6 kHz)";
		claro.s.densidade = 1.0f;
		cenaCorre(claro, wav, taxa, canais, teto, st, seq);

		// 6. ar sozinho, para o ruido nao se esconder atras de uma nota.
		Cena ar = novaCena("so ar (velocidade 1)", segPedida);
		ar.s.velocidade = 1.0f;
		cenaCorre(ar, wav, taxa, canais, teto, st, seq);
		aeLog("  esperado: ruido passa-alto, rms baixo e pico muitas vezes o rms"
			" - uma nota tem pico igual a raiz de 2 do rms, o ruido nao tem forma");

		// 7. pulsos: o que uma entrada de corpo faz soar.
		Cena puls = novaCena("pulsos de corpo novo", segPedida);
		puls.s.energia = 0.5f; puls.s.coerencia = 1.0f; puls.pulsos = 1;
		cenaCorre(puls, wav, taxa, canais, teto, st, seq);
		aeLog("  esperado: notas da pentatonica de La a partir de 57 (A3), +12 no topo");

		// 8. a rampa: o motor desliga-se a meio de uma cena cheia. A promessa e
		// que ela nao faca um passo maior do que a propria onda ja fazia: um
		// corte seco daria, no quadro do corte, um salto do tamanho do pico
		// (0,09), porque a amostra passaria de cheia a zero; a rampa de 120 ms
		// deixa o segundo tempo igual ao primeiro, e a cauda depois dela em
		// silencio exacto. As tres contas que a cena imprime separam as tres
		// doencas possiveis: o `quando` acusa o corte, a `fronteira de bloco`
		// acusa o degrau do limiter, a `cauda` acusa o motor que nao se cala.
		// A primeira metade serve de controlo: e o mesmo som sem corte nenhum.
		Cena rampa = novaCena("rampa de mute", segPedida);
		rampa.s.energia = 1.0f; rampa.s.coerencia = 1.0f; rampa.s.ambiente = 1.0f;
		for (int z = 0; z < CV_ZONES; z++) rampa.s.zona[z] = 0.9f;
		rampa.cortaA_meio = true;
		cenaCorre(rampa, wav, taxa, canais, teto, st, seq);

		// 9. a cena de referencia: a sala toda dentro, brilhante, com o master no
		// lugar. Esta nao existe para provar nada do codigo, existe para ser o
		// denominador do CV_AUDIO_ESCALA - e contra o rms dela que a escala foi
		// escolhida, e por isso que ela tem de continuar impressa: se alguem
		// mexer num ganho e o numero dela bouger, a calibracao deixou de valer.
		Cena refer = novaCena("REFERENCIA cheia m=1", segPedida);
		refer.s.energia = 1.0f; refer.s.densidade = 1.0f; refer.s.velocidade = 1.0f;
		refer.s.coerencia = 1.0f; refer.s.ambiente = 1.0f;
		for (int z = 0; z < CV_ZONES; z++) refer.s.zona[z] = 1.0f;
		refer.pulsos = 1;
		cenaCorre(refer, wav, taxa, canais, teto, st, seq);
		aeLog("  esperado: pico segurado no teto (0,1258 = -18 dBFS) pelo limiter,"
			" NAO o 0,19 cru da escala; as amostras todas dentro de 0 dBFS");

		// 10. o travao: master 2 com tudo no teto, que e o que acontece quando
		// alguem carrega em `m` ate ao fim no meio de uma sala cheia. O que
		// aqui se espera NAO e pico baixo: e o limiter a fazer o que diz.
		Cena cheio = novaCena("master 2, tudo no teto", segPedida);
		cheio.s.energia = 1.0f; cheio.s.densidade = 1.0f; cheio.s.velocidade = 1.0f;
		cheio.s.coerencia = 1.0f; cheio.s.ambiente = 1.0f; cheio.s.master = 2.0f;
		for (int z = 0; z < CV_ZONES; z++) cheio.s.zona[z] = 1.0f;
		cheio.pulsos = 1;
		cenaCorre(cheio, wav, taxa, canais, teto, st, seq);
		// o que se espera desta linha e um autoGain ABAIXO de 1,0 e um pico
		// segurado no teto: e a prova de que o limiter fala quando o master lhe
		// pede mais do que a sala aprovou, em vez de deixar o 0 dBFS entrar.
		aeLog("  autoGain no fim das duas ultimas cenas: "
			+ std::to_string(st.autoGain).substr(0, 6) + " (1,0 = nunca ceifou)");

		// o ficheiro e uma so faixa com as cenas por esta ordem, para ele poder
		// ouvir tudo de seguida sem abrir dez ficheiros
		const long quadros = (long)(wav.size() / (size_t)canais);
		std::string erro;
		double picoFicheiro = 0.0;
		long ceifFicheiro = 0;
		const std::string nome = "cv0_audio_eng_teste.wav";
		if (escreveWav(nome, wav.data(), quadros, canais, (int)taxa, erro,
			&picoFicheiro, &ceifFicheiro)) {
			aeLog("TESTE gravado: " + caminhoAbsoluto(nome) + " | "
				+ std::to_string(quadros).substr(0, 8) + " quadros a "
				+ std::to_string((int)taxa) + " Hz, " + std::to_string(canais)
				+ " canais, PCM16 | pico do ficheiro "
				+ std::to_string(picoFicheiro).substr(0, 6) + " | ceifados na copia "
				+ std::to_string(ceifFicheiro));
		} else {
			aeLog("TESTE NAO gravou: " + erro);
		}
		aeLog("=== limite honesto do teste: ele mede o que esta funcao escreve, nao o que "
			"sai da caixa. O que sai da caixa ja se sabe que sai - ele ouviu. ===");
	}

}

// ==================== contexto ====================
// Tudo o que as duas threads partilham vive aqui. A definicao fica fora do
// cabecalho, e fora do espaco de nomes anonimo de cima, de proposito: nem o
// ofApp nem o HUD precisam de saber o que e um RtAudio, e uma definicao de
// classe membro de AudioEngine nao pode viver dentro de outro espaco de nomes.
//
// Produtor e consumidor sao sempre um e um: a main thread escreve os atomicos
// e a cabeca da fila, o motorista escreve o rabo e as contas. Nao ha lock
// nenhum porque nao ha dois escritores do mesmo numero.
struct AudioEngine::Ctx {
	RtAudio* ra = nullptr;
	std::string api;
	int taxa = 48000;
	int canais = 2;
	int buffer = CV_AUDIO_BUFFER;

	// o teto ja em amplitude linear, publicado pelo setup e lido pelo callback
	float nivel = 0.1259f;
	// a copia do `activo_` do objeto: a main thread poe-a aqui uma vez por
	// frame e o motorista le-a. O estado verdadeiro e um so, e quem o muda e a
	// tecla `u` atraves de AudioEngine::toggle.
	std::atomic<bool> activo{ true };

	std::atomic<float> pE{ 0.0f }, pD{ 0.0f }, pV{ 0.0f };
	std::atomic<float> pC{ 0.0f }, pA{ 0.0f }, pM{ 1.0f };
	std::atomic<float> pZ[CV_ZONES];

	std::atomic<int> cabeca{ 0 };
	std::atomic<int> rabo{ 0 };
	Evento fila[CV_AUDIO_FILA];
	std::atomic<long> perdidos{ 0 };

	// a sonda de sincronia, apontada pelo setSyncProbe antes do setup e copia
	// para aqui porque o callback so conhece o Ctx. Nulo e o estado de sempre.
	CvSyncProbe* sync = nullptr;

	Sint st;

	std::atomic<long> callbacks{ 0 };
	std::atomic<long> amostras{ 0 };
	std::atomic<long> ceifados{ 0 };
	std::atomic<long> underflow{ 0 };
	std::atomic<long> relogioParo{ 0 };
	std::atomic<long long> nanos{ 0 };
	std::atomic<float> pico{ 0.0f };
	std::atomic<float> autoGain{ 1.0f };
	std::atomic<int> vozes{ 0 };
	std::atomic<int> ultimoErro{ 0 };
	double ultimoStreamTime = -1.0;	// so a thread do motorista le e escreve

	float* dump = nullptr;
	long dumpCap = 0;
	std::atomic<long> dumpN{ 0 };
	std::atomic<bool> dumpAtivo{ false };
	std::atomic<bool> dumpCheio{ false };
	bool dumpEscrito = false;
	std::string dumpNome;
};

// ==================== a thread do motorista ====================
int engCallback(void* out, void* in, unsigned nFrames, double streamTime,
	RtAudioStreamStatus status, void* userData) {
	(void)in;
	AudioEngine::Ctx* c = static_cast<AudioEngine::Ctx*>(userData);
	if (!c || !out || nFrames == 0) return 0;

	const auto t0 = std::chrono::steady_clock::now();

	// streamTime parado e o motorista a chamar sem relogio nenhum: o jeito
	// de um transporte "vivo" que nao anda, e a mesma acusacao que o teste
	// de emissao fez a si proprio.
	if (c->ultimoStreamTime == streamTime) c->relogioParo.fetch_add(1);
	c->ultimoStreamTime = streamTime;
	if (status & RTAUDIO_OUTPUT_UNDERFLOW) c->underflow.fetch_add(1);

	// O instantaneo do frame, lido em atomicos individuais. Uma mistura de
	// dois frames e possivel e nao faz nada de que a orelha se queixe.
	Snap s;
	s.energia = c->pE.load();
	s.densidade = c->pD.load();
	s.velocidade = c->pV.load();
	s.coerencia = c->pC.load();
	s.ambiente = c->pA.load();
	s.master = c->pM.load();
	for (int z = 0; z < CV_ZONES; z++) s.zona[z] = c->pZ[z].load();
	s.activo = c->activo.load();

	// Drena a fila toda: um bloco e 10,7 ms e a main publica a ~30 Hz, pelo
	// que dois ou tres blocos se seguem sem nada novo e um com tudo.
	Evento ev[CV_AUDIO_FILA];
	int nEv = 0;
	int rabo = c->rabo.load();
	const int cabeca = c->cabeca.load();
	while (rabo != cabeca && nEv < CV_AUDIO_FILA) {
		ev[nEv++] = c->fila[rabo];
		rabo = (rabo + 1) % CV_AUDIO_FILA;
	}
	c->rabo.store(rabo);

	// A sonda de sincronia, se alguem a armou. O instante e o da entrada no
	// callback, nao o de agora: o que se quer saber e ha quanto tempo este bloco
	// espera pelos numeros que a main publicou, e desde quando este corpo nasceu.
	// Nada aqui aloca, chama OF ou toca no sintetizador.
	if (c->sync) {
		const long long ts = CvSyncProbe::de(t0);
		c->sync->cbFim(ts);
		for (int i = 0; i < nEv; i++) c->sync->nota(ev[i].nasceUs, ts);
	}

	sintetiza(static_cast<float*>(out), nFrames, c->canais, (double)c->taxa,
		s, c->st, ev, nEv, (double)c->nivel);

	// O dump copia o que o motorista levou. Nada de I/O aqui: o ficheiro e
	// escrito mais tarde, na main thread, por isso a copia tem de caber
	// toda num buffer reservado no setup.
	if (c->dumpAtivo.load()) {
		const long falta = c->dumpCap - c->dumpN.load();
		const long take = falta < 0 ? 0 : (falta < (long)nFrames ? falta : (long)nFrames);
		if (take > 0) {
			memcpy(c->dump + (size_t)c->dumpN.load() * (size_t)c->canais, out,
				(size_t)take * (size_t)c->canais * sizeof(float));
			c->dumpN.fetch_add(take);
		}
		if (c->dumpN.load() >= c->dumpCap) {
			c->dumpAtivo.store(false);
			c->dumpCheio.store(true);
		}
	}

	c->callbacks.fetch_add(1);
	c->amostras.fetch_add((long)nFrames);
	c->pico.store((float)c->st.pico);
	c->autoGain.store(c->st.autoGain);
	c->vozes.store(c->st.vozesVivas);
	c->ceifados.fetch_add(c->st.ceifado);
	const auto dt = std::chrono::steady_clock::now() - t0;
	c->nanos.fetch_add((long long)std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
	return 0;
}

// ==================== abertura ====================
AudioEngine::AudioEngine() {}

AudioEngine::~AudioEngine() {
	// O fecho mora aqui alem do exit() do app porque um RtAudio que fica vivo
	// com a thread do motorista a apontar para um Ctx destruido nao e um
	// vazamento inocente: e uma escreitura para memoria que ja nao existe.
	close();
}

// =========================
void AudioEngine::setSyncProbe(CvSyncProbe* s) {

	// Tem de ser antes do setup(): o contexto nasce la dentro e e ele que o
	// callback ve. Apontar depois de a rota estar aberta deixaria os primeiros
	// blocos sem carimbo, que sao justamente os que se quer ver.
	sync_ = s;
}

bool AudioEngine::setup() {

	cx_.reset(new Ctx());
	Ctx& c = *cx_;
	c.sync = sync_;
	for (int z = 0; z < CV_ZONES; z++) c.pZ[z].store(0.0f);
	c.nivel = (float)pow(10.0, dbfs_ / 20.0);

	const std::string pedido = envStr("CV0_AUDIOENG");
	if (pedido == "off" || pedido == "0") {
		estado_ = "desligada por CV0_AUDIOENG";
		aeLog(estado_ + " | nenhuma rota aberta, nenhuma amostra escrita");
		return false;
	}

	// O teto da sala. dBFS e a unica unidade que ele tem de saber mudar sem
	// estar a adivinhar o que "nivel 0,4" quer dizer num codec de portatil.
	const std::string nivel = envStr("CV0_AUDIOENG_NIVEL");
	if (!nivel.empty()) {
		const double v = atof(nivel.c_str());
		if (v >= -40.0 && v <= -6.0) {
			dbfs_ = v;
			c.nivel = (float)pow(10.0, dbfs_ / 20.0);
			aeLog("CV0_AUDIOENG_NIVEL=" + nivel + ": teto a " + std::to_string(dbfs_).substr(0, 5)
				+ " dBFS = amplitude maxima " + std::to_string((double)c.nivel).substr(0, 6));
		} else {
			aeLog("CV0_AUDIOENG_NIVEL=\"" + nivel + "\" nao esta em -40..-6 dBFS: fica "
				+ std::to_string(dbfs_).substr(0, 5));
		}
	}

	// O teste offline vem antes de qualquer abertura de rota: ele nao precisa
	// do dispositivo, e se a rota derrubar o processo os numeros do motor ja
	// foram lidos.
	const std::string teste = envStr("CV0_AUDIOENG_TEST");
	if (!teste.empty() && teste != "off" && teste != "0") testeOffline(teste);

	// O dump da saida viva. Uma alocacao aqui e nenhuma no caminho do
	// motorista, que e a regra que o resto do ficheiro todo segue.
	double dumpSeg = 0.0;
	const std::string dump = envStr("CV0_AUDIOENG_WAV");
	if (!dump.empty() && dump != "off" && dump != "0") {
		dumpSeg = atof(dump.c_str());
		if (dumpSeg <= 0.0) dumpSeg = 5.0;
		if (dumpSeg > 30.0) { dumpSeg = 30.0; aeLog("CV0_AUDIOENG_WAV cortado a 30 s (teto do buffer)"); }
		c.dumpCap = (long)(dumpSeg * 48000.0);
		c.dump = new float[(size_t)c.dumpCap * 2u];
		c.dumpNome = caminhoAbsoluto("cv0_audio_eng_vivo.wav");
		c.dumpAtivo.store(true);
		aeLog("dump pedido: " + std::to_string((long)dumpSeg) + " s = "
			+ std::to_string(c.dumpCap) + " quadros reservados | " + c.dumpNome);
	}

	// ASIO primeiro porque e o unico que nesta maquina nao depende do servico
	// morto; WASAPI e DirectSound depois porque o motor nao pode ser ASIO-only
	// para sempre - uma sala com o audio saudavel e a rota certa e la.
	static const RtAudio::Api ordem[] = {
		RtAudio::WINDOWS_ASIO, RtAudio::WINDOWS_WASAPI, RtAudio::WINDOWS_DS
	};
	static const char* tokens[] = { "asio", "wasapi", "ds" };

	std::string motivos;
	for (int i = 0; i < 3; i++) {
		std::string motivo;
		if (abreRota((int)ordem[i], tokens[i], motivo)) {
			// a reserva do dump foi feita a 48 kHz, o padrao medido das tres
			// medicoes desta pacote; se a rota abriu a outra taxa o que muda e
			// a duracao gravada, nao o que se ouve, e e ela que se imprime
			if (c.dump && c.taxa != 48000) {
				delete[] c.dump;
				c.dumpCap = (long)(dumpSeg * (double)c.taxa);
				c.dump = new float[(size_t)c.dumpCap * 2u];
				aeLog("dump re-reservado para a taxa real: " + std::to_string(c.dumpCap)
					+ " quadros a " + std::to_string(c.taxa) + " Hz");
			}
			tickProx_ = (double)ofGetElapsedTimef() + 5.0;
			tickT0_ = (double)ofGetElapsedTimef();
			aeLog("motor na rota " + std::string(tokens[i]) + " | " + nome_ + " | "
				+ std::to_string(c.taxa) + " Hz | " + std::to_string(c.canais) + " canais"
				+ " | buffer " + std::to_string(c.buffer) + " quadros | teto "
				+ std::to_string(dbfs_).substr(0, 5) + " dBFS = "
				+ std::to_string((double)c.nivel).substr(0, 6)
				+ " | a rampa e de " + std::to_string(CV_AUDIO_FADE_S).substr(0, 4) + " s");
			aeLog("o que cada sinal faz esta escrito no topo de src/audioEngine.cpp; a idade"
				" do pulso vem do cabo MIDI, para as duas saidas dizerem o mesmo no mesmo frame");
			return true;
		}
		motivos += (motivos.empty() ? "" : " ; ") + motivo;
		aeLog("rota " + std::string(tokens[i]) + " nao abriu: " + motivo);
	}

	estado_ = "nenhuma rota abriu";
	aeLog("MOTOR MUDO: " + motivos + " | nenhuma amostra vai ser escrita; a peca"
		" continua igual a si propria sem som");
	if (c.dump) { delete[] c.dump; c.dump = nullptr; c.dumpAtivo.store(false); }
	return false;
}

// =========================
bool AudioEngine::abreRota(int apiInt, const char* token, std::string& motivo) {

	Ctx& c = *cx_;
	const RtAudio::Api api = (RtAudio::Api)apiInt;
	std::string ultimo;
	RtAudio* ra = new RtAudio(api, [&c](RtAudioErrorType t, const std::string&) {
		// so o codigo: o texto que o RtAudio traz e uma string e o callback pode
		// vir da thread do motorista, onde nenhuma string se pode montar
		c.ultimoErro.store((int)t);
	});
	if (ra->getCurrentApi() != api) {
		delete ra;
		motivo = std::string(token) + ": nao compilado";
		return false;
	}

	for (unsigned int id : ra->getDeviceIds()) {
		RtAudio::DeviceInfo d = ra->getDeviceInfo(id);
		if (d.outputChannels == 0) continue;

		RtAudio::StreamParameters p;
		p.deviceId = d.ID;
		p.nChannels = d.outputChannels < 2u ? d.outputChannels : 2u;
		p.firstChannel = 0;

		// float32 e o unico formato tentado, e a limita vai dita em voz alta no
		// fecho desta nota: um int16 pediria uma segunda linha de sintese e uma
		// segunda prova, e o que esta provado nesta maquina - o ASIO4ALL a
		// abrir em float32 - nao deixa espaco para adivinhar o resto.
		unsigned int taxas[3] = { d.preferredSampleRate, 48000u, 44100u };
		for (int t = 0; t < 3; t++) {
			if (taxas[t] == 0) continue;
			if (t > 0 && taxas[t] == taxas[t - 1]) continue;
			unsigned int buf = CV_AUDIO_BUFFER;
			RtAudio::StreamOptions opt;
			opt.flags = 0;
			opt.numberOfBuffers = 0;
			const RtAudioErrorType e = ra->openStream(&p, nullptr, RTAUDIO_FLOAT32,
				taxas[t], &buf, &engCallback, &c, &opt);
			if (e != RTAUDIO_NO_ERROR) {
				ultimo = erroNome(e) + " " + ra->getErrorText();
				continue;
			}
			const RtAudioErrorType s = ra->startStream();
			if (s != RTAUDIO_NO_ERROR && s != RTAUDIO_WARNING) {
				ultimo = d.name + ": abriu mas nao comecou - " + erroNome(s);
				ra->closeStream();
				continue;
			}

			c.ra = ra;
			c.api = token;
			c.taxa = (int)taxas[t];
			c.canais = (int)p.nChannels;
			c.buffer = (int)buf;
			c.ultimoStreamTime = -1.0;
			nome_ = d.name;
			estado_ = "aberta";
			abriu_ = true;
			return true;
		}
	}

	delete ra;
	motivo = std::string(token) + ": " + (ultimo.empty() ? "nenhum dispositivo com saida" : ultimo);
	return false;
}

// ==================== frame ====================
void AudioEngine::publish(const CvSignals& sig, float master, const BlobRegistry& blobs,
	int idadePulso) {

	if (!cx_ || !abriu_) return;
	Ctx& c = *cx_;

	// os envelopes do frame que acabou de ser medido, e mais nada. O master e o
	// rMaster do render, nao um segundo calculo. O `activo` tambem sobre aqui:
	// um store por frame, e o motorista nunca le um membro do objeto a que nao
	// tem direito.
	c.pE.store(sig.energyE.v);
	c.pD.store(sig.densityE.v);
	c.pV.store(sig.speedE.v);
	c.pC.store(sig.coherenceE.v);
	c.pA.store(sig.ambientE.v);
	c.pM.store(master);
	for (int z = 0; z < CV_ZONES; z++) c.pZ[z].store(sig.zoneE[z].v);
	c.activo.store(activo_.load());

	// O gatilho e a mesma regra do cabo, com o mesmo numero de idade: um corpo
	// que se fragmentou e voltou a nascer nao da nota em nenhum dos dois lados,
	// e um corpo de verdade da nos dois no mesmo frame.
	const int n = blobs.count();
	for (int i = 0; i < n; i++) {
		if (blobs.ageAt(i) != idadePulso) continue;

		const CvBlobBox& b = blobs.boxAt(i);
		const float cx = ofClamp((b.x0 + b.x1) * 0.5f, 0.0f, 0.999f);
		const float cy = ofClamp((b.y0 + b.y1) * 0.5f, 0.0f, 0.999f);
		const float area = ofClamp((b.x1 - b.x0) * (b.y1 - b.y0) * 3.0f, 0.0f, 1.0f);

		Evento e;
		e.nota = (unsigned char)(CV_AUDIO_BASE_NOTA + CV_AUDIO_PENT[(int)(cx * 5.0f)]
			+ (cy < 0.5f ? 12 : 0));
		e.vel = (unsigned char)(40 + (int)(area * 87.0f));
		e.pan = (unsigned char)(cx * 255.0f);

		// o carimbo do nascimento segue com a nota, e o que permite ao motorista
		// dizer "deste corpo ao som que dele saiu vao X ms" sem que a main e ele
		// tenham de concordar em mais nada. Sem sonda, nasceUs fica zero e o
		// Evento e byte a byte o que sempre foi nos tres campos que ele le.
		if (c.sync) {
			e.nasceUs = blobs.bornUsAt(i);
			c.sync->notaPublicada(e.nasceUs, CvSyncProbe::agora());
		}

		// o anel e de um produtor e um consumidor, por isso o espaco vazio e
		// sempre um slot a menos e o que nao cabe e contado, nunca perdido em
		// silencio: um nascimento sem nota e o mesmo genero de bug que o debounce
		// veio corrigir, e tem de aparecer.
		const int h = c.cabeca.load();
		const int r = c.rabo.load();
		if ((h + 1) % CV_AUDIO_FILA == r) {
			c.perdidos.fetch_add(1);
			continue;
		}
		c.fila[h] = e;
		c.cabeca.store((h + 1) % CV_AUDIO_FILA);
	}
}

// =========================
void AudioEngine::tick() {

	if (!cx_) return;
	Ctx& c = *cx_;

	// O despejo do dump: I/O de ficheiro na main thread, uma vez, quando o
	// buffer enche. Na thread do motorista isto seria um estalo na sala.
	if (c.dump && c.dumpCheio.load() && !c.dumpEscrito) gravaDump();

	if (!abriu_) return;
	const double agora = (double)ofGetElapsedTimef();
	if (agora < tickProx_) return;

	const long cb = c.callbacks.load();
	const long am = c.amostras.load();
	const double relogio = c.ra ? c.ra->getStreamTime() : 0.0;
	const double custo = cb > 0 ? (double)c.nanos.load() / (double)cb / 1e6 : 0.0;
	custoMs_ = custo;

	// a janela real desta linha: o relogio do stream e o tempo de parede vao
	// ambos em diferenca, porque comparar o acumulado do processo com cinco
	// segundos diria "anda" para sempre depois de ele ter parado de andar
	const double parede = tickT0_ < 0.0 ? 5.0 : agora - tickT0_;
	const double relogioJanela = tickT0_ < 0.0 ? relogio : relogio - tickRel_;

	// o pico em dBFS, numa pilha e sem alocar: um pico a zero nao e o -1800 de
	// um logaritmo, e o silencio, e dizer silencio e o que esta linha tem de
	// dizer quando a sala esta vazia
	const float picoBl = c.pico.load();
	char dbBuf[16];
	if (picoBl > 1e-6f) snprintf(dbBuf, sizeof(dbBuf), "%.1f", 20.0 * log10((double)picoBl));
	else snprintf(dbBuf, sizeof(dbBuf), "silencio");

	aeLog("callbacks " + std::to_string(cb - tickCb_) + " nos ultimos "
		+ std::to_string(parede).substr(0, 4) + " s | relogio do stream "
		+ std::to_string(relogioJanela).substr(0, 4) + " s (acumulado "
		+ std::to_string(relogio).substr(0, 5) + ") | amostras " + std::to_string(am - tickAm_)
		+ " | pico " + std::to_string((double)picoBl).substr(0, 6) + " (" + dbBuf + " dBFS)"
		+ " | ceifados " + std::to_string(c.ceifados.load() - tickCeif_)
		+ " | autoGain " + std::to_string((double)c.autoGain.load()).substr(0, 5)
		+ " | vozes " + std::to_string(c.vozes.load())
		+ " | underflow " + std::to_string(c.underflow.load())
		+ " | relogio parado " + std::to_string(c.relogioParo.load())
		+ " | dentro do callback " + std::to_string(custo).substr(0, 5) + " ms"
		+ (c.perdidos.load() > 0 ? " | PING PERDIDOS " + std::to_string(c.perdidos.load()) : ""));

	tickCb_ = cb;
	tickAm_ = am;
	tickCeif_ = c.ceifados.load();
	tickT0_ = agora;
	tickRel_ = relogio;
	tickProx_ = agora + 5.0;

	// as duas acusacoes do teste de emissao, repetidas aqui porque um motor
	// que o motorista chama mas nao anda e um motor mudo com numeros bonitos
	if (parede > 1.0 && cb > 0 && relogioJanela < 0.2 * parede)
		aeLog("VACUO: o relogio do stream andou " + std::to_string(relogioJanela).substr(0, 4)
			+ " s em " + std::to_string(parede).substr(0, 4)
			+ " s de parede. O pico e nosso; o que o driver entregou e zero.");
	else if (cb == 0)
		aeLog("VACUO: " + std::to_string(parede).substr(0, 4)
			+ " s e nenhum callback. A rota abriu e nao chamou o motorista.");
}

// =========================
void AudioEngine::gravaDump() {

	Ctx& c = *cx_;
	if (!c.dump || c.dumpEscrito) return;
	c.dumpEscrito = true;

	const long quadros = c.dumpN.load();
	std::string erro;
	double picoFicheiro = 0.0;
	long ceifFicheiro = 0;
	if (escreveWav(c.dumpNome, c.dump, quadros, c.canais, c.taxa, erro,
		&picoFicheiro, &ceifFicheiro)) {
		aeLog("dump gravado: " + c.dumpNome + " | " + std::to_string(quadros)
			+ " quadros (" + std::to_string((double)quadros / (double)c.taxa).substr(0, 4)
			+ " s) a " + std::to_string(c.taxa) + " Hz, " + std::to_string(c.canais)
			+ " canais | pico medido no ficheiro " + std::to_string(picoFicheiro).substr(0, 6)
			+ " | ceifados na copia " + std::to_string(ceifFicheiro));
		aeLog("o dump e uma copia do que o motorista levou, tirada depois do teto e do limiter:"
			" medir aqui e medir o que a sala ouviu, nao o que o codigo queria");
	} else {
		aeLog("dump NAO gravou: " + erro);
	}
}

// ==================== fecho ====================
void AudioEngine::close() {

	if (!cx_) return;
	Ctx& c = *cx_;

	if (c.ra) {
		if (c.ra->isStreamOpen()) {
			c.ra->stopStream();
			c.ra->closeStream();
		}
		delete c.ra;
		c.ra = nullptr;
		abriu_ = false;
		estado_ = "fechada";
		aeLog("motor fechado | callbacks " + std::to_string(c.callbacks.load())
			+ " | amostras " + std::to_string(c.amostras.load())
			+ " | ceifados " + std::to_string(c.ceifados.load()));
	}

	// um dump a meio nao se deita fora: escreve-se o que la vai
	if (c.dump && !c.dumpEscrito) gravaDump();
	if (c.dump) { delete[] c.dump; c.dump = nullptr; }
}

// ==================== leitura ====================
float AudioEngine::pico() const { return cx_ ? cx_->pico.load() : 0.0f; }
long AudioEngine::ceifado() const { return cx_ ? cx_->ceifados.load() : 0; }
long AudioEngine::callbacks() const { return cx_ ? cx_->callbacks.load() : 0; }
long AudioEngine::amostras() const { return cx_ ? cx_->amostras.load() : 0; }
int AudioEngine::vozes() const { return cx_ ? cx_->vozes.load() : 0; }
float AudioEngine::autoGain() const { return cx_ ? cx_->autoGain.load() : 1.0f; }
long AudioEngine::pingPerdidos() const { return cx_ ? cx_->perdidos.load() : 0; }
int AudioEngine::taxa() const { return cx_ ? cx_->taxa : 0; }
int AudioEngine::canais() const { return cx_ ? cx_->canais : 0; }
