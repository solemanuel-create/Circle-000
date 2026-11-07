#pragma once

#include "blobModel.h"
#include "cvModel.h"

#include <atomic>
#include <memory>
#include <string>

// Cabecalho trocado por apontador opaco: o motor de sintese nao precisa de
// saber o que uma sonda de sincronia e, e o que a sonda mede nao pode depender
// de o motor ter aberto ou nao.
class CvSyncProbe;

// ==================== motor de sintese (pacote 5, passo 3) ====================
// O teste de emissao respondeu, por ouvido dele em 2026-11-06, a pergunta que
// nenhuma medicao responde: sai som da caixa desta maquina. Sai - por ASIO4ALL,
// sobre o endpoint Realtek, a -18 dBFS. Este modulo poe musica em cima desse
// caminho: o barramento `sig`, que ate aqui era apenas o que o render ve, passa
// a ser tambem o que a sala ouve.
//
// Tres regras governam o modulo inteiro, e nenhuma delas e ornamentacao:
//
//   o mesmo numero, no mesmo frame. O motor nao reconta sinal nenhum. Le os
//         envelopes que `updateSignals` acabou de seguir e o `rMaster` que o
//         `applyGains` acabou de decidir, publicados uma vez por frame. E por
//         isso que o audio nao tem envelope proprio: se a imagem e o som lessem
//         escalas diferentes, a plateia perceberia o descompasso em vez de
//         perceber a reacao, que e exatamente o que o blueprint do pacote 4
//         pediu para nao acontecer.
//
//   nada do openFrameworks dentro do callback. Quem escreve no motorista e a
//         thread do proprio driver de audio, chamada a ~94 Hz (512 quadros a
//         48 kHz). Ali nao se chama OF, nao se aloca, nao se espera por mutex e
//         nao se aponta para o registo de blobs. O que a main thread sabe vai
//         para atomicos; o que a thread do driver produz e uma conta.
//
//   o travao e visivel. O teto de saida e -18 dBFS e ele e o proprio ponto de
//         clip - nao o 0 dBFS da placa, que esta 18 dB acima do que foi
//         aprovado e nunca seria alcancado. Ha uma rampa de 120 ms em vez de um
//         corte seco, e um bloco que passou do teto perde 10% de ganho e diz
//         isso no HUD. Um limiter que nao se anuncia e uma mudanca de volume
//         que ninguem pediu.
//
// O que cada sinal faz ao som e escolha estetica, nao medida: esta escrito na
// TABELA de baixo, linha a linha, com a razao. Os unicos numeros que vieram de
// medida sao a idade a que um corpo pulsa (a mesma do cabo MIDI, para as duas
// saidas nao dizerem coisas diferentes no mesmo frame) e o teto em dBFS (o
// nivel que ele acabou de ouvir e aprovar).

// O teto de saida. -18 dBFS e o nivel do tom que abriu a caixa nesta maquina;
// assumir o mesmo teto e a unica forma de o motor novo nao ser, a primeira
// vez que toca, mais alto do que aquilo que ele ja ouviu. `CV0_AUDIOENG_NIVEL`
// muda-o em dBFS e vive fora desta linha porque e um travao de sala, nao da
// peca.
static const double CV_AUDIO_TETO_DBFS = -18.0;

// 512 quadros: o mesmo buffer com que o ASIO4ALL abriu nas tres medicoes deste
// pacote. Nao e a menor latencia possivel, e o que esta medido a funcionar com
// zero underflow nesta maquina.
static const int CV_AUDIO_BUFFER = 512;

// A rampa do liga/desliga e do mute da tecla. 120 ms e curta o bastante para
// parecer imediata em cena e longa o bastante para que uma senoide a 220 Hz
// nao seja cortada a meio de um periodo, que e o que faz o clique.
static const double CV_AUDIO_FADE_S = 0.12;

// A nota de um corpo que entra e um ping com esta constante de tempo. 0,6 s e
// o suficiente para a plateia ouvir "entrou alguem" antes que o proximo
// nascimento se confunda com ela.
static const double CV_AUDIO_PING_TAU_S = 0.6;

// Vozes de ping simultaneas e fila de eventos entre a main thread e o
// motorista. A fila e do tamanho do CV_MAX_BLOBS porque um frame pode dar
// todos os nascimentos de uma vez, e o motorista so a consome no proximo
// bloco; o que nao couber e contado em `pingPerdidos`, nao descartado em
// silencio.
static const int CV_AUDIO_PING_VOZES = 8;
static const int CV_AUDIO_FILA = CV_MAX_BLOBS;

// Os graus da grade 3x2. As tres colunas sao a triada de La menor (0, 3, 7
// semitons) e as duas linhas sao a oitava, com a linha de CIMA a ser a
// oitava de cima: a mesma direcao do espelho visual que ele ja usa no
// projeto - o que esta mais alto na tela esta mais alto no som, o que esta a
// esquerda esta a esquerda no palco sonoro. Uma triada e o motivo de a
// combinacao nunca soar errada: dois corpos em colunas diferentes dao duas
// notas que pertencem ao mesmo acorde, seja qual for o par.
static const int CV_AUDIO_GRAUS[CV_ZONE_COLS] = { 0, 3, 7 };

// Os pulsos dos corpos que entram usam a pentatonica de La (0, 3, 5, 7, 10).
// E a escala mais prudente para uma nota que ninguem escolheu: a posicao do
// corpo decide o grau, e cinco graus semitone distantes nunca dao a segunda
// menor que uma triada aberta em toda a largura do quadro pode dar.
static const int CV_AUDIO_PENT[5] = { 0, 3, 5, 7, 10 };

// A3. O drone esta uma oitava abaixo (110 Hz = A2) e as vozes da grade e os
// pulsos vivem a partir daqui, porque um codec de portatil com o painel em
// Realtek(R) Audio nao da nada utile abaixo de 110 Hz.
static const int CV_AUDIO_BASE_NOTA = 57;

// Ganhos nominais de cada camada, antes do teto. Estao aqui, e nao dentro da
// conta, porque a pergunta "porque esta o colchao mais alto que as zonas?"
// tem de poder ser respondida por um numero e mudada por uma linha.
static const float CV_AUDIO_G_DRONE = 0.22f;
static const float CV_AUDIO_G_ZONA = 0.10f;
static const float CV_AUDIO_G_PING = 0.09f;
static const float CV_AUDIO_G_AR = 0.06f;

// A escala de saida, e o unico numero daqui que veio de uma medicao. O teste
// offline da primeira versao mediu a cena de referencia - seis zonas a 1,
// ambiente 1, brilho 1, master 1 - com pico 0,076 e rms 0,020, ou seja, 13 dB
// abaixo do rms do tom de -18 dBFS que ele ouviu e aprovou. Tocar mais baixo
// do que aquilo que ele acabou de aprovar nao e conservadorismo, e outra peca.
//
// 2,5 poe essa referencia a 0,19 de pico: acima do teto, portanto segurada por
// ele pelo limiter, e com a sala cheia e QUIETA (so o colchao, ambiente 1) a
// 0,056 - sete dB por baixo. Ou seja: a escala escolhe onde o travao comeca a
// falar. Uma sala que so respira nao o toca; uma sala que se enche toda e
// brilha bate-lhe e fica la. Abaixo do travao o master faz o som subir ou
// descer como sempre; acima dele quem manda e o travao, e o HUD diz isso com
// o autoGain a descer e o contador de ceifados a subir.
//
// Sem este numero os quatro ganhos de cima sao relativos e nada diz a que
// volume o conjunto sai.
static const float CV_AUDIO_ESCALA = 2.5f;

class AudioEngine {

public:

	// O contexto partilhado pelas duas threads vive todo no .cpp, mas o tipo
	// tem de ser publico: quem escreve no motorista e uma funcao livre do
	// ficheiro, chamada pelo RtAudio por apontador, e ela aponta para aqui.
	struct Ctx;

	AudioEngine();
	~AudioEngine();

	// Le as variaveis de ambiente, corre o teste offline se ele foi pedido e
	// abre a primeira rota que abrir, por ASIO, WASAPI, DirectSound - nesta
	// ordem e por este motivo: o ASIO e o unico que nao depende do servico de
	// audio desta maquina, mas numa sala com o servico vivo o WASAPI e a rota
	// certa, e o motor nao pode ser ASIO-only para sempre.
	//
	// Devolve false sem estado de erro escondido. Uma abertura que falha em
	// silencio e exatamente a armadilha do ofRtAudioSoundStream: o app fica
	// mudo e a tela nao diz nada.
	bool setup();

	// fecha o stream e despeja o dump, se ele estiver pedido. Chamado no
	// `exit()` e tambem, se a janela encher, pelo `tick()`.
	void close();

	// Publica o frame no sitio que a thread do driver le. Nao faz sintese
	// nenhuma e nao aloca: sao stores atomicos e a fila dos pulsos.
	//
	// `idadePulso` vem do chamador, e nao de uma constante deste ficheiro, de
	// proposito: e `midiOut.idade()`. As duas saidas tem de pulsar no mesmo
	// frame pelo mesmo motivo pelo qual leem o mesmo envelope - se o corpo que
	// entra der som localmente no frame 3 e no cabo no frame 5, o patch atras
	// do `Live TD` ouve outra pessoa.
	void publish(const CvSignals& sig, float master, const BlobRegistry& blobs, int idadePulso);

	// De cinco em cinco segundos imprime a leitura da rota viva (callbacks,
	// relogio, amostras, pico, ceifados, custo dentro do callback) e grava o
	// dump quando ele se enche. A inercia da medicao e a mesma do churn: uma
	// diferenca de fotografia, nao um acumulado, e a propria linha a acusar o
	// caso em que o motorista chamou mas o relogio nao andou.
	void tick();

	// Tecla `u`: poe mudo com a rampa, sem fechar nada. Ao contrario do `o` do
	// MIDI, aqui ha mesmo silencio do lado de fora - um corte seco num
	// amplificador e um clique na sala, e por isso ele nao e um booleano
	// directo no ganho e sim o alvo da rampa.
	void toggle() { activo_.store(!activo_.load()); }

	// Sonda de sincronia (CV0_SYNC): apontar-lhe com uma sonda e o unico modo
	// de o callback do motorista ter com que carimbar o instante em que leu o
	// que a main publicou. Com null - o estado de sempre, e o estado sem a
	// variavel - nenhuma linha dos canais e escrita e o Evento leva nasceUs
	// zero, que a sonda le como desarmada.
	void setSyncProbe(CvSyncProbe* s);

	bool activo() const { return activo_.load(); }
	bool abriu() const { return abriu_; }

	const std::string& nome() const { return nome_; }
	const std::string& estado() const { return estado_; }
	double nivelDbfs() const { return dbfs_; }

	// Os numeros da rota viva, lidos por nome pelo HUD. Todos atomicos e todos
	// escritos pela thread do driver; getters fora do ficheiro porque nenhum
	// deles pode existir sem o tipo do contexto estar completo.
	float pico() const;
	long ceifado() const;
	long callbacks() const;
	long amostras() const;
	int vozes() const;
	float autoGain() const;
	long pingPerdidos() const;
	double custoMs() const { return custoMs_; }
	int taxa() const;
	int canais() const;

private:

	// A ordem de tentativa das rotas. Cada chamada e autonoma: abre um RtAudio
	// proprio, percorre os dispositivos e, se uma rota pegar, fica com o objeto
	// vivo dentro do contexto; se nao, deita-o fora. Nao ha um objeto unico
	// partilhado porque o RtAudio so aceita uma API por instancia.
	//
	// O backend vai como `int` e nao como `RtAudio::Api` de proposito: este
	// cabecalho e incluido pelo ofApp, e nada aqui precisa de saber que existe
	// uma biblioteca de audio por baixo - o tipo dela fica todo no .cpp.
	bool abreRota(int api, const char* token, std::string& motivo);

	// O despejo do dump pedido por CV0_AUDIOENG_WAV: I/O de ficheiro, por isso
	// so pode ser chamado pela main thread, no tick ou no fecho.
	void gravaDump();

	std::unique_ptr<Ctx> cx_;

	std::string nome_;
	std::string estado_;
	bool abriu_ = false;
	double dbfs_ = CV_AUDIO_TETO_DBFS;
	double custoMs_ = 0.0;

	std::atomic<bool> activo_{ true };

	// a sonda apontada por fora, nula enquanto ninguem a armou. Vive no objeto
	// e copiada para o Ctx no setup, porque o Ctx nasce la e o motorista so
	// conhece o Ctx.
	CvSyncProbe* sync_ = nullptr;

	// o relogio da proxima impressao e a fotografia anterior dos contadores. A
	// janela e medida, nao assumida: `tickT0_` e a hora da linha anterior e e a
	// diferenca que se compara ao relogio do stream, porque um acumulado desde
	// o inicio do processo diria "o relogio andou" muito tempo depois de ele
	// ter parado de andar.
	double tickProx_ = 0.0;
	double tickT0_ = -1.0;
	double tickRel_ = 0.0;
	long tickCb_ = 0;
	long tickAm_ = 0;
	long tickCeif_ = 0;
};
