#pragma once

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

// O Kinect Xbox 360 (modelo 1473) nao e filtro DirectShow: ele fala pelo
// NuiApi do Kinect SDK v1.8, uma COM interface propria. Este header nao expoe
// nenhum tipo do SDK - o INuiSensor e os handles vivem no Impl dentro de
// kinect1473.cpp - pelo mesmo motivo do cvDevices: header de driver envenena
// macro e o ofApp nao precisa saber nada disso.
class Kinect1473 {

public:

	Kinect1473();
	~Kinect1473();

	// S_OK so se o SDK, o servico e um sensor estao la. O note volta em texto
	// legivel para o HUD: em cena nao ha consola aberta.
	bool open(std::string& note);
	void close();
	bool ready() const { return ready_; }

	// Bits: 1 = cor nova, 2 = profundidade nova. O 1473 entrega cor a 15 fps
	// e profundidade a 30, entao os dois streams tem ritmos diferentes e o
	// chamador precisa saber qual deles encheu o que. O mapa de paralaxe e
	// construido aqui, uma vez, no primeiro frame de profundidade.
	int grab(cv::Mat& color, cv::Mat& depthMm);

	// Profundidade -> cor: para cada pixel de profundidade, o indice do pixel
	// de cor correspondente, ou -1 quando o SDK mandou para fora do quadro.
	const std::vector<int>& map() const { return map_; }
	bool mapOk() const { return mapOk_; }
	std::string mapNote;
	std::string statusNote;

	int width() const { return W_; }
	int height() const { return H_; }

private:

	struct Impl;
	Impl* impl_;

	// profundidade -> cor, calculado uma vez por abertura com a distancia de
	// referencia; sem argumento porque a view area vem do primeiro frame
	void buildMap();

	bool ready_;
	bool mapOk_;
	int W_;
	int H_;
	std::vector<int> map_;
};
