#pragma once

#include "ofMain.h"

// Um pixel deste app anda por tres espacos diferentes e hoje as contas de
// trocar de um para o outro estavam escritas em seis lugares, com duas regras
// de reducao duplicadas (uma no rastreio por movimento, outra no rastreio por
// profundidade) que precisavam bater entre si para a mascara cair no corpo.
// Este struct e o unico dono desses numeros.
//
//	fonte   srcW x srcH   o que o shader, o fbo e a textura veem
//	maquina maskW x maskH o que o rastreio produz, em geral menor
//	janela  fit           o retangulo onde a fonte cabe sem cortar
//
// Nao ha estado de tempo aqui: e so geometria, e da isso que o resto precisa.
struct CvSpace {
	int srcW = 0;
	int srcH = 0;
	int maskW = 0;
	int maskH = 0;
	ofRectangle fit;

	bool hasSource() const { return srcW > 0 && srcH > 0; }

	void setSource(int w, int h) { srcW = w; srcH = h; }

	// A unica regra de reducao: a largura pedida manda e a altura guarda a
	// proporcao. trackW <= 0 ou >= da fonte significa maquina do tamanho da
	// fonte, que e o comportamento da tecla x.
	void setMaskFromTrack(int wSrc, int hSrc, int trackW) {
		maskW = wSrc;
		maskH = hSrc;
		if (trackW > 0 && trackW < wSrc) {
			maskW = trackW;
			maskH = MAX(1, (int)lround((float)hSrc * ((float)maskW / (float)wSrc)));
		}
	}

	// ----- maquina -> 0..1 -----
	// O MAX(1,...) nao e paranoia: sem ele uma divisao por zero no primeiro
	// frame antes de qualquer rastreio viraria NaN no uniform e o shader
	// desenharia lixo em vez de nada.
	float maskToNormX(float x) const { return x / (float)MAX(1, maskW); }
	float maskToNormY(float y) const { return y / (float)MAX(1, maskH); }

	// ----- 0..1 -> pixel de fonte -----
	float normToSrcX(float nx) const { return nx * (float)MAX(1, srcW); }
	float normToSrcY(float ny) const { return ny * (float)MAX(1, srcH); }

	// ----- pixel de fonte -> 0..1 (o retangulo sintetico vive aqui) -----
	float srcToNormX(float x) const { return x / (float)MAX(1, srcW); }
	float srcToNormY(float y) const { return y / (float)MAX(1, srcH); }

	// ----- pixel de fonte -> pixel de janela -----
	float srcToWinX(float x) const { return fit.x + x * (fit.width / (float)MAX(1, srcW)); }
	float srcToWinY(float y) const { return fit.y + y * (fit.height / (float)MAX(1, srcH)); }
	float srcToWinW(float w) const { return w * (fit.width / (float)MAX(1, srcW)); }
	float srcToWinH(float h) const { return h * (fit.height / (float)MAX(1, srcH)); }

	// ----- pixel de janela -> pixel de fonte (o caminho do rato) -----
	float winToSrcX(float x) const { return (x - fit.x) * (float)MAX(1, srcW) / MAX(1.0f, fit.width); }
	float winToSrcY(float y) const { return (y - fit.y) * (float)MAX(1, srcH) / MAX(1.0f, fit.height); }

	// A fonte inteira dentro da janela, sem corte e centrada. Era o updateFit
	// do ofApp; virou metodo porque o retangulo e dado deste struct.
	void updateFit(int winW, int winH) {
		if (!hasSource()) {
			fit.set(0, 0, winW, winH);
			return;
		}
		const float scale = ofMin((float)winW / srcW, (float)winH / srcH);
		const float fw = srcW * scale;
		const float fh = srcH * scale;
		fit.set((winW - fw) * 0.5f, (winH - fh) * 0.5f, fw, fh);
	}
};
