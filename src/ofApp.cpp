#include "ofApp.h"
#include "cvDevices.h"

namespace {
	const char* effectName(CvEffect e) {
		switch (e) {
		case CV_EFFECT_INVERT: return "INVERT";
		case CV_EFFECT_BW_THRESHOLD: return "BW UMBRAL";
		case CV_EFFECT_BW_DITHER: return "BW DITHER";
		default: return "?";
		}
	}
	const char* sourceName(CvSource s) {
		switch (s) {
		case SRC_WEBCAM: return "webcam integrada";
		case SRC_KINECT: return "kinect 360";
		case SRC_EOS: return "EOS utility";
		case SRC_NDI: return "NDI";
		default: return "?";
		}
	}
	// O ganho de normalizacao e o unico travao entre "a camera clareou o quarto"
	// e "ninguem se mexeu". Sem clamp, um escuro repentino multiplicaria o ganho
	// ate o teto e todo o frame viraria movimento.
	const float GAIN_LO = 0.6f;
	const float GAIN_HI = 1.7f;
	const double STALL_SECONDS = 1.5;

	string lower(const string& s) {
		string o = s;
		for (auto& c : o) c = (char)tolower((unsigned char)c);
		return o;
	}

	const char* backendName(int api) {
		if (api == cv::CAP_DSHOW) return "DSHOW";
		if (api == cv::CAP_MSMF) return "MSMF";
		return "?";
	}

	// O log proprio do openFrameworks derruba esta build: ofLogNotice morre na
	// primeira chamada mesmo com o link limpo, provado por marcadores antes e
	// depois de cada etapa. Toda diagnostica sai por stderr, que nao tem buffer
	// e sobrevive ao crash.
	void cv0Log(const char* tag, const string& msg) {
		fprintf(stderr, "[%s] %s\n", tag, msg.c_str());
		fflush(stderr);
	}

	// Um frame isolado nao pode enganar a leitura em cena: o primeiro depois
	// do reseed ou de um ajuste de ISO vale menos que a tendencia.
	double ema(double prev, double now) {
		return prev <= 0.0 ? now : prev * 0.8 + now * 0.2;
	}
}

// =========================
void ofApp::setup() {

	ofSetWindowTitle("Ctrl-v-0 TRACK");
	ofSetBackgroundColor(0);
	ofDisableArbTex();

	if (!img.load("0013.jpg")) {
		cv0Log("erro", "nao carregou bin/data/0013.jpg");
	}

	shaderOk = fx.load("shadersGL3/blobfx");
	if (!shaderOk) {
		cv0Log("erro", "nao carregou bin/data/shadersGL3/blobfx.(vert|frag)");
	}

	mKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));

	// Sem os nomes do DirectShow nao ha como o menu F1/F3/F4 distinguir a
	// integrada da EOS e da NDI; o indice sozinho nao diz nada em cena.
	camNames = cvListCameras();
	for (size_t i = 0; i < camNames.size(); i++) {
		cv0Log("cam", ofToString((int)i) + " \"" + camNames[i] + "\"");
	}

	chooseSourceFromEnv();
	openSource();

	blobData.assign(CV_MAX_BLOBS * 4, 0.0f);
	reseed();
}

// =========================
void ofApp::chooseSourceFromEnv() {

	// CV0_SRC escolhe a fonte no setup. Em cena isso e o que permite reabrir o
	// app ja no Kinect ou na EOS sem apalpar tecla no escuro, e e tambem a
	// unica forma de provar uma fonte que exige hardware sem mao no teclado.
	char buf[32] = { 0 };
	size_t got = 0;
	if (getenv_s(&got, buf, sizeof(buf), "CV0_SRC") != 0 || got == 0) return;

	string v = lower(string(buf));
	if (v == "kinect") params.source = SRC_KINECT;
	else if (v == "eos") params.source = SRC_EOS;
	else if (v == "ndi") params.source = SRC_NDI;
	else params.source = SRC_WEBCAM;

	cv0Log("env", "CV0_SRC=" + v + " -> " + string(sourceName(params.source)));
}

// =========================
int ofApp::findDevice(const string& needle) {

	string n = lower(needle);
	for (size_t i = 0; i < camNames.size(); i++) {
		if (lower(camNames[i]).find(n) != string::npos) return (int)i;
	}
	return -1;
}

// =========================
bool ofApp::openIndex(int idx) {

	// MSMF primeiro: medido nesta maquina, o DSHOW da camera integrada entrega
	// 15,0 fps e o MSMF 31,2 no mesmo modo. O DSHOW continua la (tecla c)
	// porque e so ele que abre o dialog de ISO/exposicao da tecla p.
	// O tamanho vai no open e no set: nenhum dos dois backends desta build
	// atende o pedido (pedido 640x480, entrega 1280x720 nos dois).
	std::vector<int> req = {
		cv::CAP_PROP_FRAME_WIDTH, params.camW,
		cv::CAP_PROP_FRAME_HEIGHT, params.camH,
		cv::CAP_PROP_FPS, params.camFps
	};

	const int primeiro = params.dshowFirst ? cv::CAP_DSHOW : cv::CAP_MSMF;
	const int segundo = params.dshowFirst ? cv::CAP_MSMF : cv::CAP_DSHOW;

	bool opened = cap.open(idx, primeiro, req);
	if (opened) capApi = primeiro;
	else {
		opened = cap.open(idx, segundo, req);
		if (opened) capApi = segundo;
	}
	if (!opened) return false;

	capIndex = idx;
	// buffersize 1 e o que segura a latencia abaixo dos 120 ms do palco
	cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

	// Uma camera que ignora o tamanho pedido nao entrega nada, e o watchdog so
	// acusaria 1.5 s depois. Provamos um frame agora e, se nao vem, pedimos o
	// tamanho que o dispositivo quiser.
	cv::Mat first;
	if (!cap.read(first) || first.empty()) {
		cap.release();
		if (cap.open(idx, primeiro)) capApi = primeiro;
		else if (cap.open(idx, segundo)) capApi = segundo;
		else return false;
		cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
		if (!cap.read(first) || first.empty()) return false;
	}

	mFrame = first;
	return true;
}

// =========================
bool ofApp::openSource() {

	closeSource();

	if (params.source == SRC_KINECT) {
		// o 1473 nao passa pelo cv::VideoCapture: e NuiApi, e o note que ele
		// devolve ja vem em frase porque sem sensor o palco precisa ler o porquê
		string note;
		kinectOk = kinect.open(note);
		capNote = note;
		if (kinectOk) {
			bgReady = false;
			lastNewFrame = ofGetElapsedTimef();
		}
		cv0Log(kinectOk ? "fonte" : "aviso", capNote);
		return kinectOk;
	}

	int idx = -1;
	if (params.source == SRC_EOS) idx = findDevice("EOS");
	else if (params.source == SRC_NDI) idx = findDevice("NDI");
	else {
		// integrada: o primeiro que nao seja um filtro virtual conhecido
		for (size_t i = 0; i < camNames.size(); i++) {
			string nm = lower(camNames[i]);
			if (nm.find("eos") == string::npos && nm.find("ndi") == string::npos
				&& nm.find("virtual") == string::npos && nm.find("obs") == string::npos) {
				idx = (int)i;
				break;
			}
		}
	}

	if (params.deviceId >= 0) idx = params.deviceId;
	// So a integrada tem reserva. Se o F3 ou o F4 caíssem no indice 0, o palco
	// receberia a webcam do laptop achando que é a EOS ou o NDI.
	if (idx < 0 && params.source == SRC_WEBCAM && !camNames.empty()) idx = 0;

	if (idx < 0) {
		capNote = string("nenhum dispositivo para \"") + sourceName(params.source) + "\"";
		cv0Log("erro", capNote);
		return false;
	}

	if (!openIndex(idx)) {
		capNote = (idx < (int)camNames.size() ? "\"" + camNames[idx] + "\" " : string())
			+ "(dev " + ofToString(idx) + ") nao abriu em DSHOW nem MSMF";
		cv0Log("erro", capNote);
		return false;
	}

	capOk = true;
	bgReady = false;
	frameNew = true;
	lastNewFrame = ofGetElapsedTimef();

	capName = (idx < (int)camNames.size()) ? camNames[idx] : string("?");
	// o que o cap.get() devolve e o que foi pedido; o mFrame e o que o
	// dispositivo realmente entrega. Em cena a diferenca e o bug.
	capNote = capName + " | " + string(backendName(capApi))
		+ " | entrega " + ofToString(mFrame.cols) + "x" + ofToString(mFrame.rows)
		+ " | pedido " + ofToString((int)cap.get(cv::CAP_PROP_FRAME_WIDTH)) + "x"
		+ ofToString((int)cap.get(cv::CAP_PROP_FRAME_HEIGHT)) + "@"
		+ ofToString((int)cap.get(cv::CAP_PROP_FPS));
	cv0Log("fonte", capNote);
	return true;
}

// =========================
void ofApp::closeSource() {

	if (cap.isOpened()) cap.release();
	if (kinectOk) kinect.close();
	kinectOk = false;
	capOk = false;
	capIndex = -1;
	capApi = 0;
	capName.clear();
	bgReady = false;
	frameNew = false;
	if (texOk) {
		texLive.clear();
		texOk = false;
	}
}

// =========================
void ofApp::update() {

	liveSource = false;
	if (capOk || kinectOk) {
		double t0 = ofGetElapsedTimeMicros();

		int kFlags = 0;
		if (kinectOk) kFlags = kinect.grab(kColor, kDepth);
		else frameNew = cap.read(mFrame) && !mFrame.empty();

		// no Kinect os dois streams tem ritmos diferentes (cor 15, profundidade
		// 30): a maquina manda no ciclo e a cor e bem-vinda quando chegar
		const bool haveColor = kinectOk ? (kFlags & 1) != 0 : frameNew;
		const bool haveFrame = kinectOk ? (kFlags & 2) != 0 : frameNew;

		double t1 = ofGetElapsedTimeMicros();

		if (haveColor) {
			if (kinectOk) cv::cvtColor(kColor, mRgb, cv::COLOR_BGRA2RGB);
			else cv::cvtColor(mFrame, mRgb, cv::COLOR_BGR2RGB);
			uploadTexture();
		}
		if (haveFrame) {
			lastNewFrame = ofGetElapsedTimef();
			frameNew = true;
			if (kinectOk) trackDepth();
			else trackFrame();
		}

		double t2 = ofGetElapsedTimeMicros();
		// dos tres numeros so um e nosso: a espera pela camera e o orcamento
		// inteiro, o rastreio cabe dentro com folga
		readMs = ema(readMs, (t1 - t0) / 1000.0);
		trackMs = ema(trackMs, (t2 - t1) / 1000.0);
		cycleMs = ema(cycleMs, ofGetLastFrameTime() * 1000.0);
		camStalled = (ofGetElapsedTimef() - lastNewFrame) > STALL_SECONDS;
		liveSource = !camStalled && texOk;
	} else {
		camStalled = false;
	}

	ofBaseDraws* s = activeSource();
	srcW = s ? (int)s->getWidth() : 0;
	srcH = s ? (int)s->getHeight() : 0;
	if (srcW <= 0 || srcH <= 0) return;

	updateFit();

	float dt = ofGetLastFrameTime();
	if (liveSource) pushCameraBlobs();
	else pushSyntheticBlobs(dt);
}

// =========================
void ofApp::uploadTexture() {

	if (mRgb.empty()) return;

	if (!texOk || texLive.getWidth() != mRgb.cols || texLive.getHeight() != mRgb.rows) {
		texLive.allocate(mRgb.cols, mRgb.rows, GL_RGB);
		texOk = true;
	}
	texLive.loadData(mRgb.data, mRgb.cols, mRgb.rows, GL_RGB);
}

// =========================
void ofApp::trackFrame() {

	// Cada operacao abaixo le e escreve o frame umas catorze vezes: a
	// resolucao do rastreio e o que manda no fps, nao a camera. O INTER_AREA
	// faz a media dos blocos que somem, entao reduzir nao engrossa o limiar.
	int gw = mRgb.cols;
	int gh = mRgb.rows;
	if (gw <= 0 || gh <= 0) return;

	int tw = gw;
	int th = gh;
	if (params.trackW > 0 && params.trackW < gw) {
		tw = params.trackW;
		th = MAX(1, (int)lround((float)gh * ((float)tw / (float)gw)));
	}
	maskW = tw;
	maskH = th;
	// o fundo vive na resolucao do rastreio: se ela mudou no voo (tecla z/x ou
	// troca de fonte), o absdiff abaixo trabalharia com dois tamanhos diferentes
	if (mBg.rows != th || mBg.cols != tw) bgReady = false;

	cv::cvtColor(mRgb, mGray, cv::COLOR_RGB2GRAY);
	if (tw != gw || th != gh) {
		cv::resize(mGray, mCur, cv::Size(tw, th), 0.0, 0.0, cv::INTER_AREA);
	} else {
		mGray.copyTo(mCur);
	}
	cv::GaussianBlur(mCur, mCur, cv::Size(5, 5), 0);

	if (!bgReady) seedBackground();

	double mc = cv::mean(mCur)[0];
	double mb = cv::mean(mBg)[0];
	curMean = (float)mc;
	bgMean = (float)mb;

	gain = (mc > 1.0 && mb > 1.0) ? ofClamp((float)(mb / mc), GAIN_LO, GAIN_HI) : 1.0f;
	mCur.convertTo(mNorm, CV_8U, gain, 0.0);

	cv::absdiff(mNorm, mBg, mDiff);
	cv::threshold(mDiff, mBin, params.motionThresh, 255, cv::THRESH_BINARY);
	cv::morphologyEx(mBin, mBin, cv::MORPH_OPEN, mKernel);
	cv::morphologyEx(mBin, mBin, cv::MORPH_DILATE, mKernel);

	// so o que nao se mexeu entra na media movel: corpo parado nao vira
	// fantasma e a luz que mudou de repente nao reescreve o fundo inteiro
	cv::bitwise_not(mBin, mStatic);
	cv::addWeighted(mBg, 1.0 - params.bgAlpha, mNorm, params.bgAlpha, 0.0, mBgTmp, CV_8U);
	mBgTmp.copyTo(mBg, mStatic);

	runContours();
}

// =========================
void ofApp::trackDepth() {

	// profundidade nao depende de luz nem de ISO: a porta de distancia e o
	// volume do palco e o fundo do quarto fica fora porque esta mais longe.
	// Nao ha modelo de fundo aqui, entao nada de ganho nem de media movel.
	if (kDepth.empty()) return;

	const int dw = kDepth.cols;
	const int dh = kDepth.rows;

	int tw = dw;
	int th = dh;
	if (params.trackW > 0 && params.trackW < dw) {
		tw = params.trackW;
		th = MAX(1, (int)lround((float)dh * ((float)tw / (float)dw)));
	}
	maskW = tw;
	maskH = th;

	cv::inRange(kDepth, cv::Scalar((double)params.kinectNearMM),
		cv::Scalar((double)params.kinectFarMM), mDepthBin);

	// o sensor de profundidade e a camera estao a poucos centimetros um do
	// outro: sem o mapa de paralaxe a caixa sai do corpo perto da borda
	mBinColor.create(dh, dw, CV_8UC1);
	const std::vector<int>& mp = kinect.map();
	if (mp.size() == (size_t)(dw * dh)) {
		// scatter: cada pixel de profundidade escreve no pixel de cor que ele ve.
		// o create() nao zera e o mapa deixa buracos (varios depth caem no mesmo
		// color), entao o chao da mascara precisa vir limpo ou o contorno nasce
		// da memoria velha.
		mBinColor.setTo(0);
		const uchar* src = mDepthBin.data;
		uchar* dst = mBinColor.data;
		for (int i = 0; i < dw * dh; i++) dst[mp[i]] = src[i];
	} else {
		mDepthBin.copyTo(mBinColor);
	}

	// INTER_NEAREST e o unico filtro honesto para mascara: area media inventaria
	// bordas que nao existem
	if (tw != dw || th != dh) {
		cv::resize(mBinColor, mBin, cv::Size(tw, th), 0.0, 0.0, cv::INTER_NEAREST);
	} else {
		mBinColor.copyTo(mBin);
	}
	cv::morphologyEx(mBin, mBin, cv::MORPH_OPEN, mKernel);
	cv::morphologyEx(mBin, mBin, cv::MORPH_DILATE, mKernel);

	kinectInside = cv::countNonZero(mBin);
	runContours();
}

// =========================
void ofApp::runContours() {

	cvBin.setFromPixels(mBin.data, maskW, maskH);

	// a area minima e fracao da imagem, entao vale o mesmo nas duas resolucoes
	int minArea = (int)(params.minAreaPct * (float)maskW * (float)maskH);
	finder.findContours(cvBin, minArea, maskW * maskH, CV_MAX_BLOBS, false, true);
}

// =========================
void ofApp::seedBackground() {

	if (mCur.empty()) return;
	mCur.copyTo(mBg);
	bgMean = (float)cv::mean(mBg)[0];
	bgReady = true;
}

// =========================
void ofApp::pushCameraBlobs() {

	scaled.clear();
	int n = (int)finder.blobs.size();
	if (n <= 0) {
		activeBlobs = 0;
		return;
	}

	// os 32 slots do shader vao para os maiores, nao para os primeiros
	vector<int> idx(n);
	for (int i = 0; i < n; i++) idx[i] = i;
	std::sort(idx.begin(), idx.end(), [&](int a, int b) {
		return finder.blobs[a].area > finder.blobs[b].area;
		});

	if (n > CV_MAX_BLOBS) n = CV_MAX_BLOBS;

	// o contorno vive na maquina reduzida: o shader quer 0..1 e a sobreposicao
	// quer pixel da fonte, cada um com seu proprio fator
	float mx = (float)srcW / (float)maskW;
	float my = (float)srcH / (float)maskH;

	for (int i = 0; i < n; i++) {
		const ofRectangle& b = finder.blobs[idx[i]].boundingRect;
		int k = i * 4;
		blobData[k + 0] = ofClamp(b.x / (float)maskW, 0.0f, 1.0f);
		blobData[k + 1] = ofClamp(b.y / (float)maskH, 0.0f, 1.0f);
		blobData[k + 2] = ofClamp((b.x + b.width) / (float)maskW, 0.0f, 1.0f);
		blobData[k + 3] = ofClamp((b.y + b.height) / (float)maskH, 0.0f, 1.0f);
		scaled.push_back(ofRectangle(b.x * mx, b.y * my, b.width * mx, b.height * my));
	}
	activeBlobs = n;
}

// =========================
void ofApp::pushSyntheticBlobs(float dt) {

	// o rato chega em coordenada de janela; o efeito vive em coordenada de fonte
	ofVec2f mouseSrc(
		(mouse.x - fitRect.x) * (float)srcW / fitRect.width,
		(mouse.y - fitRect.y) * (float)srcH / fitRect.height);

	scaled.clear();
	int n = 0;

	for (size_t i = 0; i < rects.size() && n < CV_MAX_BLOBS; i++) {

		ofRectangle& r = rects[i];
		r.x += dir[i].x * params.speed * dt;
		r.y += dir[i].y * params.speed * dt;

		if (r.x < -r.width) r.x = srcW;
		else if (r.x > srcW) r.x = -r.width;
		if (r.y < -r.height) r.y = srcH;
		else if (r.y > srcH) r.y = -r.height;

		float cx = r.x + r.width * 0.5f;
		float cy = r.y + r.height * 0.5f;

		float dist = ofDist(cx, cy, mouseSrc.x, mouseSrc.y);
		float scale = ofMap(dist, 0, params.mouseRadius, params.scaleNear, params.scaleFar, true);

		float rw = r.width * scale;
		float rh = r.height * scale;
		float x0 = cx - rw * 0.5f;
		float y0 = cy - rh * 0.5f;

		int k = n * 4;
		blobData[k + 0] = ofClamp(x0 / srcW, 0.0f, 1.0f);
		blobData[k + 1] = ofClamp(y0 / srcH, 0.0f, 1.0f);
		blobData[k + 2] = ofClamp((x0 + rw) / srcW, 0.0f, 1.0f);
		blobData[k + 3] = ofClamp((y0 + rh) / srcH, 0.0f, 1.0f);

		scaled.push_back(ofRectangle(x0, y0, rw, rh));
		n++;
	}

	activeBlobs = n;
}

// =========================
ofBaseDraws* ofApp::activeSource() {
	if (liveSource && texOk) return static_cast<ofBaseDraws*>(&texLive);
	return static_cast<ofBaseDraws*>(&img);
}

// =========================
void ofApp::draw() {

	if (shaderOk && srcW > 0 && srcH > 0) {

		if (fbo.getWidth() != srcW || fbo.getHeight() != srcH) {
			fbo.allocate(srcW, srcH, GL_RGBA);
		}

		fx.begin();
		fx.setUniform1i("uEffect", (int)params.effect);
		fx.setUniform1i("uBlobCount", activeBlobs);
		fx.setUniform1i("uFlipY", params.flipY ? 1 : 0);
		fx.setUniform1f("uThreshold", params.threshold);
		fx.setUniform1f("uDitherBias", params.ditherBias);
		fx.setUniform4fv("uBlobs", blobData.data(), CV_MAX_BLOBS);

		fbo.begin();
		ofClear(0, 255);
		if (params.showMask && liveSource && !mBin.empty()) {
			// a maquina sai menor que a fonte e o fbo tem o tamanho da fonte
			cvBin.draw(0, 0, srcW, srcH);
		} else {
			activeSource()->draw(0, 0);
		}
		fbo.end();

		fx.end();
	}

	ofSetColor(255);
	if (fbo.isAllocated()) {
		fbo.draw(fitRect.x, fitRect.y, fitRect.width, fitRect.height);
	}

	if (params.showOverlay) drawOverlay();
}

// =========================
void ofApp::drawOverlay() {

	ofNoFill();
	ofSetColor(255, 0, 0);

	float sx = fitRect.width / (float)MAX(1, srcW);
	float sy = fitRect.height / (float)MAX(1, srcH);

	for (const auto& r : scaled) {
		ofDrawRectangle(
			fitRect.x + r.x * sx,
			fitRect.y + r.y * sy,
			r.width * sx,
			r.height * sy);
	}

	ofSetColor(255);
	int y = 20;
	ofDrawBitmapString("Ctrl-v-0  |  [1] invert  [2] bw umbral  [3] bw dither", 10, y); y += 16;
	ofDrawBitmapString(ofToString(ofGetFrameRate(), 1) + " fps  |  blobs "
		+ ofToString(activeBlobs) + "  |  " + effectName(params.effect), 10, y); y += 16;
	ofDrawBitmapString("fonte F1 integrada  F2 kinect  F3 EOS  F4 NDI  |  agora: "
		+ string(sourceName(params.source)), 10, y); y += 16;

	if (liveSource) {
		ofDrawBitmapString("  " + capNote, 10, y); y += 16;
		if (kinectOk) {
			// nada de ganho nem de limiar de movimento: aqui quem decide se e
			// corpo a distancia em milimetros
			ofDrawBitmapString("  porta " + ofToString(params.kinectNearMM) + "-"
				+ ofToString(params.kinectFarMM) + " mm ( , . )  |  corpo "
				+ ofToString(kinectInside * 100 / MAX(1, maskW * maskH)) + "% do quadro", 10, y); y += 16;
			ofDrawBitmapString("  " + kinect.mapNote, 10, y); y += 16;
			ofDrawBitmapString("  " + kinect.statusNote, 10, y); y += 16;
		} else {
			ofDrawBitmapString("  mean " + ofToString(curMean, 1) + "/" + ofToString(bgMean, 1)
				+ "  |  gain " + ofToString(gain, 2)
				+ "  |  motion " + ofToString(params.motionThresh, 0) + " [ ]"
				+ "  |  alpha " + ofToString(params.bgAlpha, 2), 10, y); y += 16;
		}
		ofDrawBitmapString("  maquina " + ofToString(maskW) + "x" + ofToString(maskH)
			+ " (z/x)  |  " + string(params.showMask ? "MAQUINA" : "imagem")
			+ "  |  flipY " + ofToString(params.flipY ? 1 : 0)
			+ (kinectOk ? string("") : string("  |  modo pedido (v)  |  backend (c)  |  p abre o dialog de ISO/exposicao")), 10, y); y += 16;
		ofDrawBitmapString("  espera camera " + ofToString(readMs, 1)
			+ " ms  |  rastreio " + ofToString(trackMs, 1)
			+ " ms  |  ciclo " + ofToString(cycleMs, 1) + " ms", 10, y); y += 16;
	} else {
		ofDrawBitmapString("  sintetico (fallback)  |  umbral "
			+ ofToString(params.threshold, 2) + "  |  dither " + ofToString(params.ditherBias, 2), 10, y); y += 16;
	}

	if (!keyNote.empty()) {
		ofSetColor(255, 220, 60);
		ofDrawBitmapString(keyNote, 10, y); y += 16;
	}

	if (!capOk && !kinectOk) {
		ofSetColor(255, 60, 60);
		ofDrawBitmapString("FONTE NAO ABRIU: " + capNote, 10, y); y += 16;
	}
	if (camStalled) {
		ofSetColor(255, 60, 60);
		ofDrawBitmapString("FRAME PARADO > " + ofToString((float)STALL_SECONDS, 1)
			+ "s - watchdog assumiu, foto no lugar", 10, y); y += 16;
	}
	if (!shaderOk) {
		ofSetColor(255, 60, 60);
		ofDrawBitmapString("SHADER NAO CARREGOU - veja a consola", 10, y); y += 16;
	}
	if (!img.isAllocated()) {
		ofSetColor(255, 60, 60);
		ofDrawBitmapString("IMAGEM NAO CARREGOU - bin/data/0013.jpg", 10, y); y += 16;
	}

	ofSetColor(255);
}

// =========================
void ofApp::updateFit() {

	if (srcW <= 0 || srcH <= 0) {
		fitRect.set(0, 0, ofGetWidth(), ofGetHeight());
		return;
	}

	float scale = ofMin((float)ofGetWidth() / srcW, (float)ofGetHeight() / srcH);
	float fw = srcW * scale;
	float fh = srcH * scale;
	fitRect.set((ofGetWidth() - fw) * 0.5f, (ofGetHeight() - fh) * 0.5f, fw, fh);
}

// =========================
void ofApp::mouseMoved(int x, int y) {
	mouse.set(x, y);
}

// =========================
void ofApp::keyPressed(int key) {

	keyNote.clear();

	if (key == '1') params.effect = CV_EFFECT_INVERT;
	if (key == '2') params.effect = CV_EFFECT_BW_THRESHOLD;
	if (key == '3') params.effect = CV_EFFECT_BW_DITHER;

	if (key == OF_KEY_F1) { params.source = SRC_WEBCAM; openSource(); }
	if (key == OF_KEY_F2) { params.source = SRC_KINECT; openSource(); }
	if (key == OF_KEY_F3) { params.source = SRC_EOS; openSource(); }
	if (key == OF_KEY_F4) { params.source = SRC_NDI; openSource(); }

	if (key == 'b') { bgReady = false; seedBackground(); }
	if (key == 't') params.showMask = !params.showMask;

	// o dialog do proprio driver: e aqui que a ISO segura a luz do palco. Ele
	// so existe no DSHOW; no MSMF a tecla avisa em vez de nao fazer nada.
	if (key == 'p') {
		if (!capOk) keyNote = "p: nenhuma fonte aberta";
		else if (capApi == cv::CAP_DSHOW) cap.set(cv::CAP_PROP_SETTINGS, 1);
		else keyNote = "p: o dialog de ISO so abre no DSHOW - tecla c troca o backend";
	}

	// o outro backend: DSHOW para mexer na camera, MSMF para os 30 fps
	if (key == 'c') { params.dshowFirst = !params.dshowFirst; openSource(); }

	// o fundo do volume do Kinect: o palco inteiro ate 4,5 m e grande demais
	// para uma sala pequena, a porta de tras entra como corpo
	if (key == ',') params.kinectFarMM = ofClamp(params.kinectFarMM - 250, 1200, 6000);
	if (key == '.') params.kinectFarMM = ofClamp(params.kinectFarMM + 250, 1200, 6000);

	if (key == '[') params.motionThresh = ofClamp(params.motionThresh - 2.0f, 2.0f, 120.0f);
	if (key == ']') params.motionThresh = ofClamp(params.motionThresh + 2.0f, 2.0f, 120.0f);

	// a resolucao do rastreio e a alavanca do fps: se a luz do palco exigir o
	// frame inteiro, sobe; se o processamento atrasar, desce
	if (key == 'z') params.trackW = ofClamp(params.trackW - 160, 160, 1280);
	if (key == 'x') params.trackW = ofClamp(params.trackW + 160, 160, 1280);

	// o modo pedido ao sensor: na webcam do laptop o 720p cai a 15 fps na luz
	// normal e o 640x480 segura os 30. Reabre a fonte com o outro pedido.
	if (key == 'v') {
		if (params.camW >= 1280) { params.camW = 640; params.camH = 480; }
		else { params.camW = 1280; params.camH = 720; }
		openSource();
	}

	if (key == 'a') {
		addRect(
			ofRandom(srcW > 0 ? srcW : 640),
			ofRandom(srcH > 0 ? srcH : 480),
			ofRandom(40, 120),
			ofRandom(40, 120));
	}
	if (key == 'r') reseed();
	if (key == 'd') params.showOverlay = !params.showOverlay;
	if (key == 'f') params.flipY = !params.flipY;

	if (key == '+' || key == '=') {
		params.threshold = ofClamp(params.threshold + 0.05f, 0.0f, 1.0f);
		params.ditherBias = ofClamp(params.ditherBias + 0.05f, -1.0f, 1.0f);
	}
	if (key == '-' || key == '_') {
		params.threshold = ofClamp(params.threshold - 0.05f, 0.0f, 1.0f);
		params.ditherBias = ofClamp(params.ditherBias - 0.05f, -1.0f, 1.0f);
	}

	if (key == 's') {
		ofSaveScreen("cv0_" + ofToString(ofGetFrameNum()) + ".png");
	}
}

// =========================
void ofApp::windowResized(int w, int h) {
	updateFit();
}

// =========================
void ofApp::reseed() {
	rects.clear();
	dir.clear();
	scaled.clear();
	for (int i = 0; i < 20; i++) {
		addRect(
			ofRandom(img.getWidth()),
			ofRandom(img.getHeight()),
			ofRandom(40, 120),
			ofRandom(40, 120));
	}
}

// =========================
void ofApp::addRect(float x, float y, float w, float h) {

	if (w <= 0) w = 1;
	if (h <= 0) h = 1;

	rects.push_back(ofRectangle(x, y, w, h));

	float angle = ofRandom(TWO_PI);
	dir.push_back(ofVec2f(cos(angle), sin(angle)));
}
