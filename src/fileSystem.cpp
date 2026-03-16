#include "fileSystem.h"

void FileSystem::initDevelopment()
{
	this->basePath = utils::getExePath();
	this->isBigFileMode = false;
	this->isDevelopmentMode = true;
}

void FileSystem::initUseBigFile(std::wstring bigFileName) {
	this->basePath = utils::getExePath();
	this->isBigFileMode = true;
	this->isDevelopmentMode = false;

	FILE* file;
	if (_wfopen_s(&file, (basePath + bigFileName).c_str(), L"rb")) {
		MessageBox(NULL, L"Did you remove stuff.bin file?!", L"Error", MB_OK);
		PostQuitMessage(EXIT_FAILURE);
	}

	uint32_t nameSize;
	while (fread(&nameSize, sizeof(uint32_t), 1, file) > 0) {

		wchar_t buffer[256];
		fread(buffer, sizeof(wchar_t), nameSize, file);
		buffer[nameSize] = 0;

		uint32_t fileSize;
		fread(&fileSize, sizeof(uint32_t), 1, file);

		unsigned char* data = new unsigned char[fileSize];
		fread(data, 1, fileSize, file);

		OpenedFile of;

		of.data = data;
		of.name = std::wstring(buffer);
		of.size = fileSize;

		files.push_back(of);
	}

	fclose(file);
}

void FileSystem::Cleanup()
{

	if (isDevelopmentMode)
	{
		dumpTheBigFile(this->basePath + L"stuff.bin");
	}

	for (auto file : files) {
		if (file.data != nullptr) {
			delete[] file.data;
			file.data = nullptr;
		}
	}
}

long long getFileSize(std::wstring filename)
{
	struct _stat64 stat_buf;
	int rc = _wstat64(filename.c_str(), &stat_buf);
	return rc == 0 ? stat_buf.st_size : -1;
}

FileHandle FileSystem::openFile(std::wstring fileName) {

	if (isBigFileMode)
	{
		for (size_t i = 0; i < files.size(); i++) {
			if (files[i].name == fileName) return i;
		}
	}
	else
	{
		std::wstring fullName = basePath + fileName;

		OpenedFile file;
		errno_t hr = _wfopen_s(&file.fileOSHandle, fullName.c_str(), L"rb");

		if (hr != 0 || file.fileOSHandle == nullptr) {
			utils::validate(E_FAIL, (L"Could not open file " + fileName).c_str());
			return size_t(-1);
		}
		file.name = fileName;
		file.size = getFileSize(basePath + file.name);

		files.push_back(file);

		return files.size() - 1;
	}

	return INVALID_FILE_HANDLE;
}

bool FileSystem::readFromFile(FileHandle file, size_t size, void* buffer, size_t bufferSize) {

	if (isBigFileMode)
	{
		memcpy(buffer, files[file].data + files[file].offset, size);
		files[file].offset += size;

		return files[file].offset <= files[file].size;
	}
	else
	{
		return (size == fread_s(buffer, bufferSize, 1, size, files[file].fileOSHandle));
	}
}

unsigned char* FileSystem::readAll(FileHandle file) {

	size_t size;
	return readAll(file, size);
}

unsigned char* FileSystem::readAll(FileHandle file, size_t& fileSize) {

	if (isBigFileMode) {
		fileSize = files[file].size;
		return files[file].data;
	}
	else
	{
		if (files[file].data == nullptr)
		{
			if (files[file].size == -1) {
				utils::validate(E_FAIL, L"File was loaded with invalid size.");
			}

			files[file].data = new unsigned char[files[file].size];

			if (!files[file].data) {
				utils::validate(E_FAIL, L"File was loaded with invalid data.");
			}

			if (files[file].size != fread(files[file].data, 1, files[file].size, files[file].fileOSHandle)) {
				utils::validate(E_FAIL, L"Failure when reading file from disk.");
			}
		}

		fileSize = files[file].size;
		return files[file].data;
	}
}

void FileSystem::dumpTheBigFile(std::wstring fileName) {

	FILE* file;
	_wfopen_s(&file, fileName.c_str(), L"wb");

	for (auto f : files) {
		uint32_t nameSize = (uint32_t)f.name.length();
		uint32_t fileSize = (uint32_t)f.size;

		fwrite(&nameSize, sizeof(uint32_t), 1, file);
		fwrite(f.name.data(), sizeof(wchar_t), nameSize, file);
		fwrite(&fileSize, sizeof(uint32_t), 1, file);

		FILE* ff;
		_wfopen_s(&ff, (basePath + f.name).c_str(), L"rb");

		char* temp = new char[fileSize];

		fread(temp, 1, fileSize, ff);

		fclose(ff);

		fwrite(temp, 1, fileSize, file);
	}

	fclose(file);
}