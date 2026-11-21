#pragma once

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

namespace cv0log {

inline std::mutex outputMutex;
inline std::ofstream outputFile;

inline bool openFile(const std::string& path, std::string& error) {
	std::lock_guard<std::mutex> lock(outputMutex);
	if (outputFile.is_open()) outputFile.close();
	outputFile.clear();
	outputFile.open(path, std::ios::out | std::ios::trunc);
	if (outputFile) return true;
	error = "nao foi possivel abrir " + path;
	return false;
}

inline void closeFile() {
	std::lock_guard<std::mutex> lock(outputMutex);
	if (outputFile.is_open()) {
		outputFile.flush();
		outputFile.close();
	}
}

inline void write(const char* tag, const std::string& message) {
	std::lock_guard<std::mutex> lock(outputMutex);
	std::fprintf(stderr, "[%s] %s\n", tag, message.c_str());
	std::fflush(stderr);
	if (!outputFile.is_open()) return;

	outputFile << '[' << tag << "] " << message << '\n';
	outputFile.flush();
	if (!outputFile) {
		std::fprintf(stderr, "[logger] falha ao gravar arquivo de diagnostico\n");
		std::fflush(stderr);
		outputFile.close();
	}
}

inline void printfLine(const char* tag, const char* format, ...) {
	char message[2048] = {};
	va_list args;
	va_start(args, format);
	const int written = std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (written < 0) {
		write(tag, "falha ao formatar mensagem de diagnostico");
		return;
	}
	message[sizeof(message) - 1] = '\0';
	write(tag, message);
}

}
