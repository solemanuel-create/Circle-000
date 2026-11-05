#pragma once

#include "ofMain.h"
#include "ofxOpenCv.h"
#include "kinect1473.h"

// Os inteiros abaixo sao o contrato com o uniform uEffect do blobfx.frag.
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

static const int CV_MAX_BLOBS = 32;

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
	int kinectNearMM = 1000;
	int kinectFarMM = 4500;
};

class ofApp : public ofBaseApp {

public:
	void setup();
	void update();
	void draw();

	void keyPressed(int key);
	void mouseMoved(int x, int y);
	void windowResized(int w, int h);

	void addRect(float x, float y, float w, float h);
	void reseed();

	ofImage img;
	ofFbo fbo;
	ofShader fx;
	CvParams params;

	ofRectangle fitRect;
	ofVec2f mouse;

	vector<ofRectangle> rects;
	vector<ofRectangle> scaled;
	vector<ofVec2f> dir;
	vector<float> blobData;

	bool shaderOk = false;
	int activeBlobs = 0;

	void updateFit();
	void drawOverlay();

	// ----- camera -----
	// O videoInput do openFrameworks morre ao abrir o dispositivo nesta
	// instalacao (SIGSEGV em listDevices e em setup). O cv::VideoCapture le
	// frames reais, entao a entrada passa por ele e o nome dos filtros vem do
	// ICreateDevEnum.
	cv::VideoCapture cap;
	cv::Mat mFrame, mRgb;
	ofTexture texLive;
	vector<string> camNames;
	bool capOk = false;
	bool texOk = false;
	bool frameNew = false;
	int capIndex = -1;
	int capApi = 0;
	string capName;
	string capNote;
	// aviso de uma tecla: vive ate a proxima, para o 'p' poder dizer que o
	// backend atual nao tem dialog
	string keyNote;

	// ----- Kinect 360 modelo 1473 -----
	// Cor vai para a mesma textura da webcam; profundidade vai para a mesma
	// maquina de contornos. So a origem dos pixels muda.
	Kinect1473 kinect;
	bool kinectOk = false;
	cv::Mat kColor, kDepth;
	cv::Mat mDepthBin, mBinColor;
	int kinectInside = 0;

	ofxCvGrayscaleImage cvBin;
	ofxCvContourFinder finder;

	// mGray e o cinza do frame inteiro; mCur em diante vivem na resolucao de
	// rastreio (params.trackW), por isso as coordenadas dos blobs precisam das
	// dimensoes da maquina para voltar ao espaco da fonte.
	cv::Mat mGray, mCur, mNorm, mBg, mDiff, mBin, mStatic, mKernel, mBgTmp;
	int maskW = 0;
	int maskH = 0;

	bool bgReady = false;
	bool camStalled = false;
	bool liveSource = false;
	double lastNewFrame = 0.0;
	float curMean = 0.0f;
	float bgMean = 0.0f;
	float gain = 1.0f;
	int srcW = 0;
	int srcH = 0;

	// Onde vai o orcamento do frame decide o que corrigir: se a espera pela
	// camera manda, e o modo do sensor (resolucao/exposicao); se o rastreio
	// manda, e a resolucao da maquina. Os tres numeros vao para o HUD.
	double readMs = 0.0;
	double trackMs = 0.0;
	double cycleMs = 0.0;

	int findDevice(const string& needle);
	void chooseSourceFromEnv();
	bool openSource();
	void closeSource();
	bool openIndex(int idx);
	void trackFrame();
	void trackDepth();
	void runContours();
	void uploadTexture();
	void seedBackground();
	void pushCameraBlobs();
	void pushSyntheticBlobs(float dt);
	ofBaseDraws* activeSource();
};
