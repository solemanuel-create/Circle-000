#pragma once

#include "ofMain.h"

// O modelo de dados do rastreio: o que uma fonte entrega, o que o barramento
// mede e o que o operador ajusta. Vive fora do ofApp de proposito porque duas
// coisas diferentes precisam dele - a maquina de silhueta (maskPipeline) e o
// render - e nenhuma das duas deve incluir ofApp.h para ler um parametro.
//
// Os ids abaixo identificam, eles nao descrevem. O que cada fonte e cada efeito
// fazem mora nas suas tabelas (sourceTable.cpp, effectTable.cpp); o inteiro que
// vai para o uniform uEffect nao e a posicao aqui, e um dado da linha do efeito.
enum CvEffect {
	CV_EFFECT_INVERT = 0,
	CV_EFFECT_BW_THRESHOLD = 1,
	CV_EFFECT_BW_DITHER = 2,
	CV_EFFECT_COUNT = 3
};

// F1..F4 na ordem que ele declarou: integrada, Kinect, EOS, NDI.
// Os tres primeiros sao filtros DirectShow abertos pelo cv::VideoCapture; o
// Kinect 1473 nao e, ele fala pelo NuiApi do Kinect SDK v1.8 (kinect1473.cpp).
enum CvSource {
	SRC_WEBCAM = 0,
	SRC_KINECT = 1,
	SRC_EOS = 2,
	SRC_NDI = 3,
	SRC_COUNT = 4
};

// O frame tem duas origens e nao uma. A palavra `liveSource` respondia as duas
// perguntas sozinha, e cada leitor inventava o sentido: o draw perguntava "que
// pixels vao para o fbo?", o rastreio perguntava "de onde vem a maquina de
// contornos?", e o HUD perguntava "que tempos estou mostrando?". Hoje as duas
// respostas coincidem, e e por isso que a coincidencia pode ser escrita uma vez
// so, a partir dos fatos medidos, e lida por nome. O caso em que elas dao
// respostas diferentes ja existe em palco: com o frame parado o watchdog manda a
// foto para a tela e o baralho de retangulos assume o rastreio.
struct CvFrameOrigin {
	bool pixelsFromSource = false;	// senao: a foto no lugar
	bool tracksSource = false;		// senao: os rectangulos do baralho
};

// Um numero so para os dois lados do uBlobs. O C++ enche estes slots todos e o
// frag declara os mesmos 32 num #define que nao se pode importar: quem confere
// uma coisa com a outra e o BlobRegistry::verifyFrag, uma vez no setup, e um
// desacordo e um aviso no HUD em vez de silhuetas a menos em silencio.
static const int CV_MAX_BLOBS = 32;

// 3 colunas x 2 linhas. Nao e uma grade de vigilancia: e o minimo que
// distingue "o publico esta todo de um lado" de "a sala inteira se mexeu",
// que e a diferenca entre um cue de zona e um cue coletivo.
static const int CV_ZONE_COLS = 3;
static const int CV_ZONE_ROWS = 2;
static const int CV_ZONES = CV_ZONE_COLS * CV_ZONE_ROWS;

// O envelope e o unico lugar onde o tempo de resposta mora. Ataque curto e
// decaimento longo porque a plateia precisa ver a reacao comecar no instante
// em que o corpo se mexe e ver o resto ir embora devagar; e porque imagem e
// som tem que ler o MESMO numero no MESMO instante, o que so um motor unico
// garante. Nao ha envelope proprio de render nem de audio.
struct CvEnvelope {
	float v = 0.0f;
	float attack = 0.06f;
	float decay = 0.45f;

	void follow(float target, float dt) {
		const float tau = (target > v) ? attack : decay;
		// a forma exponencial e a unica que sobrevive a um dt que muda de
		// frame para frame: um passo fixo em segundos aceleraria com o fps
		const float k = (tau > 1e-4f && dt > 0.0f) ? (1.0f - expf(-dt / tau)) : 1.0f;
		v += (target - v) * k;
	}
};

// O barramento. Alvo cru e envelope de cada sinal, juntos, para que o audio e
// os cues leiam daqui em vez de recontar a maquina.
struct CvSignals {
	float energy = 0.0f;		// parte do quadro ocupada
	float density = 0.0f;		// area dos contornos, nao numero deles
	float speed = 0.0f;			// centróide em larguras de quadro por segundo
	float coherence = 0.0f;		// as zonas concordam entre si
	float ambient = 0.0f;		// a sala, em media lenta: o chao do sinal
	float zone[CV_ZONES] = { 0.0f };

	CvEnvelope energyE, densityE, speedE, coherenceE, ambientE;
	CvEnvelope zoneE[CV_ZONES];

	// decaimento mais longo no espaco: uma zona que se esvaziou ha dois frames
	// nao pode apagar a memoria do que estava la
	void reset() {
		energy = density = speed = coherence = ambient = 0.0f;
		for (int i = 0; i < CV_ZONES; i++) zone[i] = 0.0f;
		energyE = CvEnvelope();
		densityE = CvEnvelope();
		speedE = CvEnvelope();
		coherenceE = CvEnvelope();
		ambientE = CvEnvelope();
		for (int i = 0; i < CV_ZONES; i++) {
			zoneE[i] = CvEnvelope();
			zoneE[i].decay = 0.9f;
		}
	}
};

struct CvParams {
	CvEffect effect = CV_EFFECT_INVERT;
	float threshold = 0.5f;
	float ditherBias = 0.0f;
	float mouseRadius = 600.0f;
	float scaleNear = 3.0f;
	float scaleFar = 0.4f;
	float speed = 60.0f;
	bool showOverlay = true;
	bool flipY = false;

	// pacote 3 - fonte e tracker
	CvSource source = SRC_WEBCAM;
	bool showMask = false;
	int deviceId = -1;
	int camW = 640;
	int camH = 480;
	int camFps = 30;
	float motionThresh = 18.0f;
	float bgAlpha = 0.05f;
	float minAreaPct = 0.004f;
	// a maquina nao precisa dos 720p inteiros: rastreiar menor e o que
	// devolve os 30 fps da camera em vez dos 15 do processamento
	int trackW = 640;

	// DSHOW e o backend do dialog de ISO, MSMF e o que entrega os 30 fps nesta
	// camera (medido: DSHOW 15,0 fps, MSMF 31,2 fps no mesmo modo 1280x720).
	bool dshowFirst = false;

	// o palco do Kinect em milimetros: corpo e o que estiver entre os dois.
	// A porta de distancia dispensa o modelo de fundo e nao depende de luz.
	// O teto e 4000 porque o sensor nao reporta nada alem disso: o fundo longe
	// sai como "sem dados" (zero) e zero ja fica fora da porta por baixo.
	int kinectNearMM = 1000;
	int kinectFarMM = 4000;

	// pacote 4 - mapa de ganhos. Cada ganho e quanto de um envelope vai para um
	// alvo de render; zero naquele alvo e o comportamento antigo, sem reacao.
	// O master e o unico travao global, porque em cena nao se desliga um sinal
	// de cada vez.
	bool reactive = true;
	float master = 1.0f;
	float gainExposure = 0.35f;
	float gainGrow = 0.60f;
	float gainThresh = 0.12f;
	float gainDither = 0.20f;
	float gainSpeed = 60.0f;
};
