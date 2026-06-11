#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
}

//--------------------------------------------------------------
void ofApp::update() {
}

//--------------------------------------------------------------
void ofApp::draw() {

	ofSetColor(
		ofRandom(255),
		ofRandom(255),
		ofRandom(255));

	float tamanho = ofMap(mouseX, 0, ofGetWidth(), 10, 200);

	ofDrawCircle(mouseX, mouseY, tamanho);

	ofSetColor(255);
	ofDrawBitmapString("SOLE audiovisual", 20, 20);
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key) {
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) {
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y) {
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y) {
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg) {
}
