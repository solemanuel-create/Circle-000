#pragma once

#include "cvModel.h"
#include "cvSpace.h"

#include "ofxOpenCv.h"

#include <vector>

// A maquina de silhueta inteira, fora do ofApp. Ela e a unica dona dos buffers
// de rastreio e de duas rotas que produzem exatamente o mesmo objeto - um
// CV_8UC1 de maskW x maskH e uma lista de contornos - a partir de duas entradas
// que nao tem nada em comum:
//
//	buildMotion  cinza do frame, normalizado pela media global, menos um fundo
//	           que e uma media movel so dos pixels que nao se mexeram
//	buildDepth   porta de distancia em milimetros, espalhada pelo mapa de
//	           paralaxe, reduzida com INTER_NEAREST
//
// Depois das duas, o que vem a seguir e identico: abrir, dilatar, contornos. E
// por isso que o barramento de sinais e o render nao precisam saber de onde o
// pixel veio, e por isso tambem que uma fonte nova custa um build* e nada mais.
//
// Nada aqui aloca por frame: os cv::Mat sao membros e o OpenCV so realoca
// quando o tamanho muda, o que acontece na troca de fonte e na tecla z/x.
class MaskPipeline {
public:
	MaskPipeline();

	// rgb e o frame ja em RGB (mRgb do app). Retorna false sem mexer em nada
	// quando nao ha pixels.
	bool buildMotion(const cv::Mat& rgb, const CvParams& p, CvSpace& sp);

	// depthMm em CV_16UC1 e colorMap o mapa de paralaxe do 1473 (indice de
	// profundidade -> indice de cor). Sem mapa do tamanho certo a rota cai na
	// identidade, que e o comportamento antigo anterior ao mapa.
	bool buildDepth(const cv::Mat& depthMm, const std::vector<int>& colorMap,
		const CvParams& p, CvSpace& sp);

	// o fundo volta a ser chao: quem trocou fonte, tamanho do sensor ou luz
	// nao pode confiar na media movel anterior
	void invalidateBackground() { bgReady = false; }
	void seedBackground();

	const cv::Mat& bin() const { return mBin; }
	const std::vector<ofxCvBlob>& blobs() const { return finder.blobs; }
	ofxCvGrayscaleImage& gray() { return cvBin; }

	// os numeros que o HUD mostra: a maquina conta, o app so leitura
	float curMean = 0.0f;
	float bgMean = 0.0f;
	float gain = 1.0f;
	int inside = 0;
	bool backgroundReady() const { return bgReady; }

private:
	void runContours(const CvParams& p, const CvSpace& sp);

	cv::Mat mGray, mCur, mNorm, mBg, mDiff, mBin, mStatic, mBgTmp;
	cv::Mat mDepthBin, mBinColor;
	cv::Mat mKernel;
	ofxCvGrayscaleImage cvBin;
	ofxCvContourFinder finder;
	bool bgReady = false;
};
