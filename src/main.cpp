#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){

	//Use ofGLFWWindowSettings for more options like multi-monitor fullscreen
	// Esta build do core morre na primeira escrita de log de qualquer nivel
	// (tambem dentro dos addons: ofxCvGrayscaleImage::setFromPixels loga ao
	// alocar). O silencio e global; a diagnostica do app sai por stderr e arquivo.
	ofSetLogLevel(OF_LOG_SILENT);

	ofGLWindowSettings settings;
	settings.setSize(1024, 768);
	settings.windowMode = OF_WINDOW; //can also be OF_FULLSCREEN
	// os shaders em bin/data/shadersGL3 so existem no renderer programavel
	settings.setGLVersion(3, 3);

	auto window = ofCreateWindow(settings);

	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();
	
}
