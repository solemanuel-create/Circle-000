#pragma once

#include "blobModel.h"
#include "cvModel.h"

#include <string>

// ==================== saida de controle MIDI (pacote 5, rota viva) ====================
// O cabo que foi medido e devolve: a porta "Live TD" do loopMIDI fechou laco com
// 24 mensagens em 31 ms, enquanto o LoopBe1 abriu as duas pontas e nao devolveu
// nada. Este modulo e o lado de la: o barramento `sig` sai pela ponta OUT e o
// que do outro lado e o TouchDesigner, o Ableton ou um modulo fisico.
//
// O que sai NAO e som. MIDI carrega numero de controle, nota e clock; um synth
// de software atras do cabo continua mudo enquanto o servico de audio desta
// maquina estiver morto. A saida serve para a plateia mexer em outra maquina,
// nao para esta maquina fazer barulho.
//
// Duas especies de mensagem, porque o barramento tem duas especies de coisa:
//
//   CC    os dez envelopes, uma vez por frame, so quando o byte muda. Manda o
//         valor ja multiplicado pelo master, porque `g` em cena e o travao de
//         tudo - se a imagem para de reagir, o cabo tambem para.
//   nota  um pulso por corpo que entra, com duracao fixa. E o que um CC nao
//         consegue dizer: "agora entrou alguem", nao "quanto de area esta la".
//         "Entrou" e uma idade, nao o frame zero - ver CV_MIDI_PULSO_IDADE.
//
// A porta e escolhida por nome, nunca por indice: `midiOutGetNumDevs` muda com o
// hardware plugado e um numero de hoje e outro cabo amanha. `CV0_MIDIOUT` da a
// agulha (o default e "Live TD") e `CV0_MIDIOUT=off` desliga o modulo inteiro.
//
// Nada aqui instala driver, abre registro ou toca servico. `winmm.lib` ja vem
// linkada pelo openFrameworksRelease.props, entao a fiao do .vcxproj e so os
// dois arquivos novos.

static const int CV_MIDI_CC = 11;	// 5 sinais + 6 zonas

// Os tres numeros abaixo sao escolha estetica, nao medida: a oitava dos pulsos,
// a largura em semitons e a duracao do note-off. Mudar qualquer um deles muda o
// que o outro lado ouve, nao muda o que esta tela mostra.
static const int CV_MIDI_NOTE_BASE = 48;	// C3
static const int CV_MIDI_NOTE_SPAN = 12;	// uma oitava pela largura do quadro
static const double CV_MIDI_NOTE_MS = 60.0;	// duracao do pulso

// Um canal so, o 1. Dois canais seria mais expressivo e menos provado: o que
// esta medido e que a porta devolve o que se manda, nao que o outro lado sabe
// separar canais.
static const int CV_MIDI_CH = 0;

// O debounce do gatilho, em frames de vida do corpo. Nao e um enfeite: a sonda
// de churn mediu 26,4 nascimentos por segundo com uma cena de gente a andar e
// a saida toca uma nota por nascimento, ou seja, a nota nao e "entrou alguem",
// e "a silhueta fragmentou-se neste frame". Um corpo que fica puxa a idade
// dele para a frente um frame de cada vez, por isso o gatilho a idade N soa
// uma vez por corpo e nao uma vez por frame.
//
// O tres nao e um gosto, e o ultimo degrau da curva de sobrevivencia que cabe
// no orcamento. A 30,6 fps - o ritmo medido nas janelas de cena cheia, nao o
// das janelas de despertar - um frame vale 32,7 ms, pelo que N=3 custa 98 ms
// e N=4 custaria 131, ja fora dos 120 ms da peca. A mesma corrida mediu o
// preço do outro lado do cabo: 21,2 notas por segundo com o gatilho no frame
// zero contra 5,5 com ele no tres, pela boca do ouvinte independente, que nao
// e este codigo. O que se perdeu foram nascimentos que duravam menos que um
// terco de segundo; o que ficou sao corpos.
//
// Este e o PADRAO, nao o unico valor possivel: CV0_MIDIAGE muda-o a partir do
// ambiente e 0 repoe exatamente o comportamento antigo, que e o que a prova
// A/B precisa de ter para poder chamar "novo" ao que mudou.
static const int CV_MIDI_PULSO_IDADE = 3;

// Cada CC e reenviado por inteiro neste intervalo mesmo sem mudar de valor, para
// que um app que abriu o ouvinte no meio da cena nao fique para sempre com o
// valor zero de quem chegou depois da festa.
static const double CV_MIDI_REENVIO_S = 0.5;

class MidiOut {

public:

	// le CV0_MIDIOUT, enumera as saidas por winmm, escolhe por nome e abre.
	// Devolve false sem estado de erro escondido: o que aconteceu vai todo para
	// as linhas de stderr e para a linha do HUD, porque uma abertura que falha
	// em silencio e a mesma armadilha do ofRtAudioSoundStream.
	bool setup();
	void close();

	// um frame de cabo: os CC que mudaram, os pulsos dos corpos novos e o
	// reenvio periodico de tudo. Nao aloca nada e nao faz conta de imagem.
	void send(const CvSignals& sig, float master, const BlobRegistry& blobs);

	// tecla 'o': desliga sem fechar a porta, para o operador poder cortar o cabo
	// no meio da cena sem pagar reabertura. O ultimo byte enviado fica onde esta
	// la do outro lado, que e o que um CC quer dizer quando para de chegar.
	void toggle() { ligado_ = !ligado_; }
	bool ligado() const { return ligado_; }
	bool abriu() const { return porta_ != nullptr; }

	// A idade que dispara o pulso. O setup le CV0_MIDIAGE e poe aqui; fora de
	// setup nada mais escreve, porque uma mudanca a meio de um A/B trocaria a
	// condicao enquanto se mede a condicao.
	void setIdade(int frames) { idade_ = frames < 0 ? 0 : frames; }
	int idade() const { return idade_; }

	// Os numeros do cabo, lidos por nome pelo HUD. A formatacao fica la porque
	// e la que vivem os buffers sem alocacao, e um getter que montasse uma string
	// por frame seria exatamente a alocacao que o P4 deitou fora.
	const std::string& nome() const { return nome_; }
	const std::string& estado() const { return estado_; }
	int enviados() const { return enviados_; }
	int recusados() const { return recusados_; }
	int ccVivos() const;
	int pulsosVivos() const;

private:

	bool escolhePorta(const std::string& agulha);
	void envia(unsigned int msg);
	void cc(int numero, int slot, float valor);
	void pulsos(const BlobRegistry& blobs);

	void* porta_ = nullptr;		// HMIDIOUT, opaco para nao trazer windows.h
	unsigned int idx_ = 0;		// a porta que a agulha escolheu, nao um pedido
	std::string nome_;			// nome real da saida aberta
	std::string estado_;		// o que a abertura respondeu, uma vez
	bool ligado_ = true;
	int enviados_ = 0;
	int recusados_ = 0;
	int ultimos_[CV_MIDI_CC];	// o ultimo byte de cada CC, -1 = nunca enviado

	// o pulso em andamento: um por slot, com o id do corpo a que pertence para
	// que um corpo que sai e volta no mesmo slot nao herde o note-off do outro
	struct Pulso {
		int id = -1;
		double ate = 0.0;
		int nota = 0;
	};
	Pulso pulsos_[CV_MAX_BLOBS];

	int idade_ = CV_MIDI_PULSO_IDADE;	// o debounce escolhido pelo ambiente ou o padrao

	double reenvio_ = 0.0;		// epoca do proximo reenvio completo
};
