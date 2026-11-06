#include "midiOut.h"

#include <windows.h>
#include <mmsystem.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "winmm.lib")

namespace {

	// O escritor proprio, pelo mesmo motivo das duas sondas: a escrita do log do
	// OF derruba esta build, e uma saida de controle que nao abre tem que dizer
	// isso em algum lugar que sobreviva a ela propria.
	void moLog(const std::string& s) {
		fprintf(stderr, "[midi-out] %s\n", s.c_str());
		fflush(stderr);
	}

	std::string lower(std::string s) {
		for (size_t i = 0; i < s.size(); i++)
			s[i] = (char)tolower((unsigned char)s[i]);
		return s;
	}

	// Uma linha por numero de controle. O que o outro lado le e o que esta aqui,
	// e e por isso que nem o emissor nem o HUD podem discordar: os dois varrem
	// esta tabela, como a ParamTable e a SourceTable fazem com as teclas.
	//
	// 1 a 5 sao os cinco sinais; 16 a 21 sao as seis zonas, escolhidos porque na
	// especificacao MIDI 16 a 23 sao os controladores de uso geral, ou seja,
	// numeros que nenhum sintetico interpreta sozinho.
	struct Linha {
		const char* nome;
		int cc;
	};
	const Linha TABELA[CV_MIDI_CC] = {
		{ "energia", 1 },
		{ "densidade", 2 },
		{ "velocidade", 3 },
		{ "coerencia", 4 },
		{ "ambiente", 5 },
		{ "zona 0", 16 },
		{ "zona 1", 17 },
		{ "zona 2", 18 },
		{ "zona 3", 19 },
		{ "zona 4", 20 },
		{ "zona 5", 21 },
	};

	// os dez envelopes na mesma ordem da tabela, lidos do barramento sem master
	// nenhum aqui dentro: quem decide o travao e o chamador
	float valorLinha(const CvSignals& s, int i) {
		switch (i) {
		case 0: return s.energyE.v;
		case 1: return s.densityE.v;
		case 2: return s.speedE.v;
		case 3: return s.coherenceE.v;
		case 4: return s.ambientE.v;
		default: return s.zoneE[i - 5].v;
		}
	}

	std::string envStr(const char* name) {
		char buf[64] = { 0 };
		size_t got = 0;
		if (getenv_s(&got, buf, sizeof(buf), name) != 0 || got == 0) return std::string();
		return std::string(buf);
	}

	const char* errNome(unsigned int e) {
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
		case 11: return "parametro invalido";
		case 12: return "handle ocupado";
		}
		return "codigo de erro";
	}

}

// ==================== abertura ====================
bool MidiOut::setup() {

	for (int i = 0; i < CV_MIDI_CC; i++) ultimos_[i] = -1;
	for (int i = 0; i < CV_MAX_BLOBS; i++) pulsos_[i].id = -1;

	// sem a variavel o default e a porta que foi medida; `off` e a unica forma
	// de desligar, porque em ensaio o cabo pode ter que ficar quieto sem que o
	// operador tenha de recompilar
	const std::string pedido = envStr("CV0_MIDIOUT");
	if (pedido == "off" || pedido == "0") {
		estado_ = "desligada por CV0_MIDIOUT";
		moLog(estado_);
		return false;
	}

	const std::string agulha = pedido.empty() ? std::string("Live TD") : pedido;
	if (!escolhePorta(agulha)) return false;

	// O debounce le-se aqui e em mais lado nenhum: e uma condicao de saida, nao
	// uma medicao, por isso vive com a saida. Um valor que nao e numero fica
	// onde estava e diz-lo em voz alta - em ensaio um typo no ambiente nao pode
	// soar como "o gatilho mudou".
	const std::string idadePedida = envStr("CV0_MIDIAGE");
	if (!idadePedida.empty()) {
		const int v = atoi(idadePedida.c_str());
		if (v < 0 || v > 60) {
			moLog("CV0_MIDIAGE=\"" + idadePedida + "\" nao e um numero de frames entre 0 e 60: fica "
				+ std::to_string(idade_));
		} else {
			idade_ = v;
			moLog("CV0_MIDIAGE=" + std::to_string(v) + ": pulso aos " + std::to_string(v)
				+ " frames de vida do corpo" + (v == 0 ? std::string(" (comportamento antigo)") : std::string()));
		}
	}

	const unsigned int r = midiOutOpen((HMIDIOUT*)&porta_, idx_, 0, 0, CALLBACK_NULL);
	if (r != 0) {
		porta_ = nullptr;
		estado_ = "midiOutOpen recusou: " + std::string(errNome(r));
		moLog(estado_ + " | saida \"" + nome_ + "\"");
		return false;
	}

	estado_ = "aberta";
	moLog("saida \"" + nome_ + "\" pela porta " + std::to_string((int)idx_)
		+ " | " + std::to_string(CV_MIDI_CC) + " CC no canal "
		+ std::to_string(CV_MIDI_CH + 1) + ", pulsos na oitava de "
		+ std::to_string(CV_MIDI_NOTE_BASE) + " aos "
		+ std::to_string(idade_) + " frames de vida");
	return true;
}

// =========================
bool MidiOut::escolhePorta(const std::string& agulha) {

	// Enumerar por nome e a regra da casa: o indice muda com o hardware plugado.
	// A lista inteira sai em stderr quando a agulha nao bate, porque "porta nao
	// encontrada" sem as opcoes e uma mensagem que obriga a rodar uma sonda.
	const unsigned int n = midiOutGetNumDevs();
	std::string vistos;
	for (unsigned int i = 0; i < n; i++) {
		MIDIOUTCAPSW caps;
		ZeroMemory(&caps, sizeof(caps));
		if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != 0) continue;
		char nome[160] = { 0 };
		WideCharToMultiByte(CP_UTF8, 0, caps.szPname, 32, nome, sizeof(nome) - 1, nullptr, nullptr);
		if (!vistos.empty()) vistos += " | ";
		vistos += "[" + std::to_string((int)i) + "] " + nome;
		if (lower(nome).find(lower(agulha)) == std::string::npos) continue;
		nome_ = nome;
		idx_ = i;
		return true;
	}

	estado_ = "nenhuma saida com \"" + agulha + "\"";
	moLog(estado_ + " | vistas " + std::to_string((int)n) + ": " + (vistos.empty() ? "nenhuma" : vistos));
	return false;
}

// =========================
void MidiOut::close() {
	if (!porta_) return;
	const unsigned int r = midiOutClose((HMIDIOUT)porta_);
	if (r != 0) moLog(std::string("midiOutClose recusou: ") + errNome(r));
	porta_ = nullptr;
}

// ==================== frame ====================
void MidiOut::send(const CvSignals& sig, float master, const BlobRegistry& blobs) {

	if (!porta_ || !ligado_) return;

	const double agora = ofGetElapsedTimef();
	const bool reenviar = agora >= reenvio_;
	if (reenviar) reenvio_ = agora + CV_MIDI_REENVIO_S;

	// O reenvio periodico nao e redundancia: um ouvinte que ligou o MIDI depois
	// de comecada a cena nunca recebeu o primeiro valor, e um CC que nunca chegou
	// vale zero para sempre la do outro lado. Limpar o byte antes de mandar e o
	// que faz o cc() ver diferenca; limpar depois reenviaria tudo em todo frame.
	for (int i = 0; i < CV_MIDI_CC; i++) {
		if (reenviar) ultimos_[i] = -1;
		cc(TABELA[i].cc, i, ofClamp(valorLinha(sig, i) * master, 0.0f, 1.0f));
	}

	pulsos(blobs);
}

// =========================
void MidiOut::cc(int numero, int slot, float valor) {

	const int byte = (int)(valor * 127.0f + 0.5f);
	if (byte == ultimos_[slot]) return;	// um frame a 60 fps com o valor parado
	ultimos_[slot] = byte;				// nao manda 11 mensagens que dizem o mesmo
	envia(0xB0u | (unsigned)CV_MIDI_CH | ((unsigned)numero << 8) | ((unsigned)byte << 16));
}

// =========================
void MidiOut::pulsos(const BlobRegistry& blobs) {

	// O pulso e do corpo que entrou, nao do slot: na camera a ordem e por area,
	// entao o slot zero pode ser outro corpo no frame seguinte. O id e o que
	// impede um note-off de calar a nota de quem ainda esta em cena.
	const double agora = ofGetElapsedTimef();

	for (int p = 0; p < CV_MAX_BLOBS; p++) {
		if (pulsos_[p].id < 0) continue;
		if (agora < pulsos_[p].ate) continue;
		envia(0x80u | (unsigned)CV_MIDI_CH | ((unsigned)pulsos_[p].nota << 8) | (64u << 16));
		pulsos_[p].id = -1;
	}

	const int n = blobs.count();
	for (int i = 0; i < n; i++) {
		// o gatilho e uma idade, nao o nascimento: a idade sobe um frame de cada
		// vez para o mesmo id, por isso esta igualdade soa uma vez por corpo.
		// Com idade_ = 0 e o comportamento de sempre - o que a prova A/B chama
		// velho - e com idade_ = N um contorno que se fragmentou e voltou a
		// nascer morre sem ter dado nota, que e tudo o que o debounce promete.
		if (blobs.ageAt(i) != idade_) continue;
		int livre = -1;
		for (int p = 0; p < CV_MAX_BLOBS; p++) {
			if (pulsos_[p].id == blobs.idAt(i)) { livre = -2; break; }
			if (pulsos_[p].id < 0 && livre < 0) livre = p;
		}
		if (livre < 0) continue;	// -2 ja tem pulso, -1 nao ha lugar

		const CvBlobBox& b = blobs.boxAt(i);
		const float cx = ofClamp((b.x0 + b.x1) * 0.5f, 0.0f, 0.999f);
		const float area = ofClamp((b.x1 - b.x0) * (b.y1 - b.y0) * 3.0f, 0.0f, 1.0f);
		const int nota = CV_MIDI_NOTE_BASE + (int)(cx * (float)CV_MIDI_NOTE_SPAN);
		const int vel = 40 + (int)(area * 87.0f);

		pulsos_[livre].id = blobs.idAt(i);
		pulsos_[livre].nota = nota;
		pulsos_[livre].ate = agora + CV_MIDI_NOTE_MS / 1000.0;
		envia(0x90u | (unsigned)CV_MIDI_CH | ((unsigned)nota << 8) | ((unsigned)vel << 16));
	}
}

// =========================
void MidiOut::envia(unsigned int msg) {

	const unsigned int r = midiOutShortMsg((HMIDIOUT)porta_, msg);
	if (r == 0) enviados_++;
	else recusados_++;
}

// ==================== leitura ====================
int MidiOut::ccVivos() const {
	int n = 0;
	for (int i = 0; i < CV_MIDI_CC; i++) if (ultimos_[i] >= 0) n++;
	return n;
}

// =========================
int MidiOut::pulsosVivos() const {
	int n = 0;
	for (int p = 0; p < CV_MAX_BLOBS; p++) if (pulsos_[p].id >= 0) n++;
	return n;
}
