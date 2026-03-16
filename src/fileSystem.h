#pragma once
#include "common.h"
#include "utils.h"

typedef size_t FileHandle;
constexpr FileHandle INVALID_FILE_HANDLE = std::numeric_limits<size_t>::max();

class FileSystem {
public:
	void initDevelopment();
	void initUseBigFile(std::wstring bigFileName);
	void Cleanup();

	FileHandle openFile(std::wstring fileName);
	bool readFromFile(FileHandle file, size_t size, void* buffer, size_t bufferSize);

	unsigned char* readAll(FileHandle file, size_t& fileSize);
	unsigned char* readAll(FileHandle file);

	void dumpTheBigFile(std::wstring fileName);

private:

	struct OpenedFile {
		FILE* fileOSHandle = nullptr;
		std::wstring name;
		size_t size = 0;
		size_t offset = 0;
		unsigned char* data = nullptr;
	};

	std::vector<OpenedFile> files;

	std::wstring basePath;
	bool isBigFileMode;
	bool isDevelopmentMode;
};