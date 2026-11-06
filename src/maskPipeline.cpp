#include "maskPipeline.h"

namespace {
	// O ganho de normalizacao e o unico travao entre "a camera clareou o quarto"
	// e "ninguem se mexeu". Sem clamp, um escuro repentino multiplicaria o ganho
	// ate o teto e todo o frame viraria movimento.
	const float GAIN_LO = 0.6f;
	const float GAIN_HI = 1.7f;
}

// =========================
MaskPipeline::MaskPipeline() {
	mKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
}

// =========================
bool MaskPipeline::buildMotion(const cv::Mat& rgb, const CvParams& p, CvSpace& sp) {

	// Cada operacao abaixo le e escreve o frame umas catorze vezes: a
	// resolucao do rastreio e o que manda no fps, nao a camera. O INTER_AREA
	// faz a media dos blocos que somem, entao reduzir nao engrossa o limiar.
	const int gw = rgb.cols;
	const int gh = rgb.rows;
	if (gw <= 0 || gh <= 0) return false;

	sp.setMaskFromTrack(gw, gh, p.trackW);
	const int tw = sp.maskW;
	const int th = sp.maskH;

	// o fundo vive na resolucao do rastreio: se ela mudou no voo (tecla z/x ou
	// troca de fonte), o absdiff abaixo trabalharia com dois tamanhos diferentes
	if (mBg.rows != th || mBg.cols != tw) bgReady = false;

	cv::cvtColor(rgb, mGray, cv::COLOR_RGB2GRAY);
	if (tw != gw || th != gh) {
		cv::resize(mGray, mCur, cv::Size(tw, th), 0.0, 0.0, cv::INTER_AREA);
	} else {
		mGray.copyTo(mCur);
	}
	cv::GaussianBlur(mCur, mCur, cv::Size(5, 5), 0);

	if (!bgReady) seedBackground();

	const double mc = cv::mean(mCur)[0];
	const double mb = cv::mean(mBg)[0];
	curMean = (float)mc;
	bgMean = (float)mb;

	gain = (mc > 1.0 && mb > 1.0) ? ofClamp((float)(mb / mc), GAIN_LO, GAIN_HI) : 1.0f;
	mCur.convertTo(mNorm, CV_8U, gain, 0.0);

	cv::absdiff(mNorm, mBg, mDiff);
	cv::threshold(mDiff, mBin, p.motionThresh, 255, cv::THRESH_BINARY);
	cv::morphologyEx(mBin, mBin, cv::MORPH_OPEN, mKernel);
	cv::morphologyEx(mBin, mBin, cv::MORPH_DILATE, mKernel);

	// so o que nao se mexeu entra na media movel: corpo parado nao vira
	// fantasma e a luz que mudou de repente nao reescreve o fundo inteiro
	cv::bitwise_not(mBin, mStatic);
	cv::addWeighted(mBg, 1.0 - p.bgAlpha, mNorm, p.bgAlpha, 0.0, mBgTmp, CV_8U);
	mBgTmp.copyTo(mBg, mStatic);

	runContours(p, sp);
	return true;
}

// =========================
bool MaskPipeline::buildDepth(const cv::Mat& depthMm, const std::vector<int>& colorMap,
	const CvParams& p, CvSpace& sp) {

	// profundidade nao depende de luz nem de ISO: a porta de distancia e o
	// volume do palco e o fundo do quarto fica fora porque esta mais longe.
	// Nao ha modelo de fundo aqui, entao nada de ganho nem de media movel.
	if (depthMm.empty()) return false;

	const int dw = depthMm.cols;
	const int dh = depthMm.rows;

	sp.setMaskFromTrack(dw, dh, p.trackW);
	const int tw = sp.maskW;
	const int th = sp.maskH;

	cv::inRange(depthMm, cv::Scalar((double)p.kinectNearMM),
		cv::Scalar((double)p.kinectFarMM), mDepthBin);

	// o sensor de profundidade e a camera estao a poucos centimetros um do
	// outro: sem o mapa de paralaxe a caixa sai do corpo perto da borda
	mBinColor.create(dh, dw, CV_8UC1);
	if (colorMap.size() == (size_t)(dw * dh)) {
		// scatter: cada pixel de profundidade escreve no pixel de cor que ele ve.
		// o create() nao zera e o mapa deixa buracos (varios depth caem no mesmo
		// color), entao o chao da mascara precisa vir limpo ou o contorno nasce
		// da memoria velha.
		mBinColor.setTo(0);
		const uchar* src = mDepthBin.data;
		uchar* dst = mBinColor.data;
		for (int i = 0; i < dw * dh; i++) dst[colorMap[i]] = src[i];
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

	inside = cv::countNonZero(mBin);
	runContours(p, sp);
	return true;
}

// =========================
void MaskPipeline::runContours(const CvParams& p, const CvSpace& sp) {

	cvBin.setFromPixels(mBin.data, sp.maskW, sp.maskH);

	// a area minima e fracao da imagem, entao vale o mesmo nas duas resolucoes
	int minArea = (int)(p.minAreaPct * (float)sp.maskW * (float)sp.maskH);
	finder.findContours(cvBin, minArea, sp.maskW * sp.maskH, CV_MAX_BLOBS, false, true);
}

// =========================
void MaskPipeline::seedBackground() {

	if (mCur.empty()) return;
	mCur.copyTo(mBg);
	bgMean = (float)cv::mean(mBg)[0];
	bgReady = true;
}
