#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <sstream>
#include <locale>
#include <vector>
#include <list>
#include <Windows.h>
#include "bitcompressor.hpp"
#include "bitarchiveinfo.hpp"
#include "bitextractor.hpp"

#pragma comment(lib, "advapi32.lib")

using namespace std;
#define SERVICE_NAME ((LPSTR)"BackupService")
#define CONFIG_PATH ("D:\\test\\config.txt")
#define LOG_PATH ("D:\\test\\log.txt")
#define BACKUP_INFO_PATH ("D:\\test\\backupinfo.txt")

SERVICE_STATUS ssStatus;
SERVICE_STATUS_HANDLE hStatus;

std::wstring w_tempDirectory = L"D:\\test\\temp\\";
string targetDirectory;
string archiveDirectory;
DWORD loopTime;
WCHAR* w_archiveDirectory;
WCHAR* w_targetDirectory;
bool firstReading = true;
FILETIME lastChangeTime;
struct FILE_INFO
{
	char fileName[MAX_PATH];
	FILETIME fileLastChangeTime;
};

void WINAPI ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv);
void ServiceInstall(void);
void ServiceRemove(void);
void __stdcall ServiceStart(void);
void ServiceStop(void);
int AddLogMessage(DWORD dwErrCode, const char* strMessage);
void WINAPI ServiceControlHandler(DWORD dwCtrl);

void MainBackupFunction(void);
int ReadConfigFile(list<string>& configStrings);
int CheckConfigFileChangeTime(void);
void MakeBackupList(list<string>& filesForBackup, list<string> configStrings, list<FILE_INFO>* fileInfoList);
bool findFileInList(list<string> filesForBackup, char* fileName);
bool isFileChanged(list<FILE_INFO>* fileInfoList, WIN32_FIND_DATA findFileData);

void MakeBackup(std::list<std::string> lsFileNames);
void MakeVectorsForCompress(std::list<std::string> lsFileNames, std::vector<std::wstring>& filesForArchivate, std::vector<std::wstring>& filesForUpdate, std::vector<std::wstring>& filesForSave);
void ExtractFilesInTemp(std::vector<std::wstring>* filesForSave);
void CompressIntoNewArchive(std::vector<std::wstring> filesForArchivate, std::vector<std::wstring> filesForUpdate, std::vector<std::wstring> filesForSave);
void CleanUp(void);


/*****************************MAIN*****************************/
void main(int argc, char* argv[])
{
	SERVICE_TABLE_ENTRY DispatchTable[] = { {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain}, {NULL, NULL} };
	if (!StartServiceCtrlDispatcher(DispatchTable))
	{
		if (ERROR_FAILED_SERVICE_CONTROLLER_CONNECT == GetLastError())
		{
			if (!strcmp(argv[1], "install"))
				ServiceInstall();
			else if (!strcmp(argv[1], "remove"))
				ServiceRemove();
			else if (!strcmp(argv[1], "start"))
				ServiceStart();
			else if (!strcmp(argv[1], "stop"))
				ServiceStop();
		}
		else
			AddLogMessage(GetLastError(), "StartServiceCtrlDispatcher");
	};
}
/*****************************MAIN*****************************/

int AddLogMessage(DWORD dwErrCode, const char* strMessage)
{
	FILE* logFile;
	if (!(logFile = fopen(LOG_PATH, "a")))
		return -1;
	fprintf(logFile, "[0x%lx = %li] %s\n", dwErrCode, dwErrCode, strMessage);
	fclose(logFile);
	return 0;
}

void WINAPI ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
	hStatus = RegisterServiceCtrlHandler(
		SERVICE_NAME,
		(LPHANDLER_FUNCTION)ServiceControlHandler);

	if (!hStatus)
	{
		AddLogMessage(GetLastError(), "RegisterServiceCtrlHandler error");
		return;
	}

	ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	ssStatus.dwCurrentState = SERVICE_START_PENDING;
	ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	ssStatus.dwWin32ExitCode = NO_ERROR;
	ssStatus.dwServiceSpecificExitCode = 0;
	ssStatus.dwWaitHint = 0;
	ssStatus.dwCheckPoint = 0;

	SetServiceStatus(hStatus, &ssStatus);
	ssStatus.dwCurrentState = SERVICE_RUNNING;
	SetServiceStatus(hStatus, &ssStatus);

	if (AddLogMessage(0, "Service started"))
	{
		ssStatus.dwCurrentState = SERVICE_STOPPED;
		ssStatus.dwWin32ExitCode = -1;
		SetServiceStatus(hStatus, &ssStatus);
		return;
	}
	MainBackupFunction();
}

void ServiceInstall(void)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;
	char strPath[MAX_PATH];

	if (!GetModuleFileName(NULL, strPath, MAX_PATH))
	{
		AddLogMessage(GetLastError(), "GetModuleFileName error");
		return;
	}
	if (!(hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE)))
	{
		AddLogMessage(GetLastError(), "OpenSCManager error");
		return;
	}
	if (!(hService = CreateService(
		hSCManager,
		SERVICE_NAME,
		SERVICE_NAME,
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_NORMAL,
		strPath,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL)))
	{
		AddLogMessage(GetLastError(), "CreateService error");
		CloseServiceHandle(hSCManager);
		return;
	}
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);
	AddLogMessage(GetLastError(), "Service installed");
}

void ServiceRemove(void)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;
	if (!(hSCManager = OpenSCManager(
		NULL,
		NULL,
		SC_MANAGER_ALL_ACCESS)))
	{
		AddLogMessage(GetLastError(), "OpenSCManager error");
		return;
	}
	if (!(hService = OpenService(
		hSCManager,
		SERVICE_NAME,
		SERVICE_STOP | DELETE)))
	{
		AddLogMessage(GetLastError(), "OpenService error");
		CloseServiceHandle(hSCManager);
		return;
	}
	if (!DeleteService(hService))
	{
		AddLogMessage(GetLastError(), "DeleteService error");
		CloseServiceHandle(hService);
		CloseServiceHandle(hSCManager);
		return;
	}
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);
	AddLogMessage(0, "Service removed");
}

void __stdcall ServiceStart(void)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;
	if (!(hSCManager = OpenSCManager(
		NULL,
		NULL,
		SC_MANAGER_CREATE_SERVICE)))
	{
		AddLogMessage(GetLastError(), "OpenSCManager error");
		return;
	}
	if (!(hService = OpenService(
		hSCManager,
		SERVICE_NAME,
		SERVICE_START)))
	{
		AddLogMessage(GetLastError(), "OpenService error");
		CloseServiceHandle(hSCManager);
		return;
	}
	if (!StartService(
		hService,
		0,
		NULL))
	{
		AddLogMessage(GetLastError(), "StartService error");
		CloseServiceHandle(hService);
		CloseServiceHandle(hSCManager);
		return;
	}
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);
	AddLogMessage(0, "Service started");
}

void ServiceStop(void)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;
	SERVICE_STATUS ssStatus;

	if (!(hSCManager = OpenSCManager(
		NULL,
		NULL,
		SC_MANAGER_ALL_ACCESS)))
	{
		AddLogMessage(GetLastError(), "OpenSCManager error");
		return;
	}
	if (!(hService = OpenService(
		hSCManager,
		SERVICE_NAME,
		SERVICE_STOP)))
	{
		AddLogMessage(GetLastError(), "OpenService error");
		CloseServiceHandle(hSCManager);
		return;
	}
	if (!ControlService(
		hService,
		SERVICE_CONTROL_STOP,
		(LPSERVICE_STATUS)&ssStatus))
	{
		AddLogMessage(GetLastError(), "ControlService error");
		CloseServiceHandle(hService);
		CloseServiceHandle(hSCManager);
		return;
	}
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);
	AddLogMessage(0, "Service stopped");
}

void WINAPI ServiceControlHandler(DWORD dwCtrl)
{
	if ((dwCtrl == SERVICE_CONTROL_STOP) ||
		(dwCtrl == SERVICE_CONTROL_SHUTDOWN))
	{
		ssStatus.dwWin32ExitCode = -1;
		ssStatus.dwCurrentState = SERVICE_STOPPED;
		SetServiceStatus(hStatus, &ssStatus);
		AddLogMessage(0, "Stop control signal");
	}
}

/**********************Check Cnahges************************/
void MainBackupFunction(void)//
{
	list<string> configStrings;
	list<FILE_INFO> fileInfoList;
	while (ssStatus.dwCurrentState == SERVICE_RUNNING)
	{
		if (CheckConfigFileChangeTime())
		{
			if (ReadConfigFile(configStrings))
			{
				AddLogMessage(0, "Error of reading config file");
				goto error;
			};
			//fileInfoList.clear();
		};
		list<string> filesForBackup;
		MakeBackupList(filesForBackup, configStrings, &fileInfoList);
		if (!filesForBackup.empty())
			MakeBackup(filesForBackup);
		else
			AddLogMessage(0, "No changes");
		Sleep(loopTime * 1000);
	};

error:
	ssStatus.dwCurrentState = SERVICE_STOPPED;
	ssStatus.dwWin32ExitCode = -1;
	SetServiceStatus(hStatus, &ssStatus);
};

int ReadConfigFile(list<string>& configStrings)
{
	configStrings.clear();
	ifstream configFile(CONFIG_PATH);
	if (configFile.fail())
	{
		AddLogMessage(0, "Config file open error");
		return -1;
	};
	getline(configFile, archiveDirectory, '\n');
	getline(configFile, targetDirectory, '\n');
	targetDirectory += "\\";
	string strLoopTime;
	getline(configFile, strLoopTime, '\n');
	loopTime = stoi(strLoopTime);
	while (!configFile.fail())
	{
		string str;
		getline(configFile, str, '\n');
		if ((str.find('*') <= str.length()) || (str.find('?') <= str.length()))
			configStrings.push_front(str);
		else
			configStrings.push_back(str);
	};
	configStrings.pop_back();
	configFile.close();
	return 0;
};

int CheckConfigFileChangeTime(void)
{
	HANDLE hFile = CreateFileA(CONFIG_PATH, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	FILETIME currentLastChangeTime;
	GetFileTime(hFile, NULL, NULL, &currentLastChangeTime);
	if (firstReading)
	{
		firstReading = false;
		lastChangeTime = currentLastChangeTime;
		CloseHandle(hFile);
		return 1;
	}
	else if (CompareFileTime(&lastChangeTime, &currentLastChangeTime))
	{
		lastChangeTime = currentLastChangeTime;
		CloseHandle(hFile);
		return 1;
	};
	CloseHandle(hFile);
	return 0;
};

void MakeBackupList(list<string>& filesForBackup, list<string> configStrings, list<FILE_INFO>* fileInfoList)
{
	for (auto iMask : configStrings)
	{
		string tempDirMask = targetDirectory + iMask;
		WIN32_FIND_DATA findFileData;
		HANDLE hFind = INVALID_HANDLE_VALUE;
		hFind = FindFirstFileA(tempDirMask.c_str(), &findFileData);
		if (hFind == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				if (isFileChanged(fileInfoList, findFileData))
				{
					if (!findFileInList(filesForBackup, findFileData.cFileName))
						filesForBackup.push_back(findFileData.cFileName);
				};
			};
		} while (0 != FindNextFileA(hFind, &findFileData));
	};
};


bool isFileChanged(list<FILE_INFO>* fileInfoList, WIN32_FIND_DATA findFileData)
{
	FILE_INFO tempFileInfo;
	if (fileInfoList->empty())
	{
		strcpy(tempFileInfo.fileName, findFileData.cFileName);
		tempFileInfo.fileLastChangeTime = findFileData.ftLastWriteTime;
		fileInfoList->push_back(tempFileInfo);
		return true;
	};

	bool search = false;
	for (auto iFileInfo = fileInfoList->begin(); iFileInfo != fileInfoList->end(); ++iFileInfo)
	{
		if (!strcmp(iFileInfo->fileName, findFileData.cFileName))
		{
			if (CompareFileTime(&iFileInfo->fileLastChangeTime, &findFileData.ftLastWriteTime))
			{
				iFileInfo->fileLastChangeTime = findFileData.ftLastWriteTime;
				return true;
			}
			else
				return false;
		};
	};
	strcpy(tempFileInfo.fileName, findFileData.cFileName);
	tempFileInfo.fileLastChangeTime = findFileData.ftLastWriteTime;
	fileInfoList->push_back(tempFileInfo);
	return true;
}

bool findFileInList(list<string> filesForBackup, char* fileName)
{
	bool result = false;
	if (filesForBackup.empty())
		return result;
	for (auto iFile : filesForBackup)
	{
		if (!strcmp(iFile.c_str(), fileName))
			result = true;
	}
	return result;
};

/*bit7z*/
using namespace bit7z;

bool isFileInVector(std::wstring name, std::vector<std::wstring> list)
{
	for (auto iStr : list)
	{
		if (name == iStr)
			return true;
	};
	return false;
}

void MakeVectorsForCompress(std::list<std::string> lsFileNames, std::vector<std::wstring>& filesForArchivate, std::vector<std::wstring>& filesForUpdate, std::vector<std::wstring>& filesForSave)
{
	Bit7zLibrary lib{ L"7z.dll" };
	BitArchiveInfo archive{ lib, w_archiveDirectory, BitFormat::Zip };

	for (auto iFileForBackup : lsFileNames)
	{
		WCHAR* wFileName = new WCHAR[iFileForBackup.length()];
		mbstowcs(wFileName, (char*)iFileForBackup.c_str(), iFileForBackup.length());
		wFileName[iFileForBackup.length()] = L'\0';
		bool search = false;
		auto arc_items = archive.items();
		for (auto& item : arc_items)
		{
			if (!wcscmp(wFileName, item.name().c_str()))
			{
				search = true;
				break;
			};
		};
		if (search)
			filesForUpdate.push_back(wFileName);
		else
			filesForArchivate.push_back(wFileName);
	};
	auto arc_items = archive.items();
	for (auto& item : arc_items)
	{
		bool search = false;
		if(filesForUpdate.empty())
			filesForSave.push_back(item.name());
		for (auto iFileName : filesForUpdate)
		{
			if (wcscmp(iFileName.c_str(), item.name().c_str()) == 0)//совпадает
			{
				search = true;
				break;
			};
		};
		if ((!search)&&(!isFileInVector(item.name(), filesForSave)))
			filesForSave.push_back(item.name());
	};
}

void ExtractFilesInTemp(std::vector<std::wstring>* filesForSave)
{
	char* tempPath = new char[w_tempDirectory.length()];
	wcstombs(tempPath, w_tempDirectory.c_str(), w_tempDirectory.length());
	tempPath[w_tempDirectory.length() - 1] = '\0';
	CreateDirectoryA(tempPath, NULL);
	Bit7zLibrary lib{ L"7z.dll" };
	BitExtractor extractor{ lib, BitFormat::Zip };
	for (auto iFileName = filesForSave->begin(); iFileName != filesForSave->end(); ++iFileName)
	{
		extractor.extractMatching(w_archiveDirectory, *iFileName, w_tempDirectory); //extracting a specific file
		*iFileName = w_tempDirectory + *iFileName;
	};
}

void CompressIntoNewArchive(std::vector<std::wstring> filesForArchivate, std::vector<std::wstring> filesForUpdate, std::vector<std::wstring> filesForSave)
{
	std::wofstream fileBackUpInfo(BACKUP_INFO_PATH);
	fileBackUpInfo << L"Last Backup in " << w_targetDirectory;

	std::vector<std::wstring> outputData;
	if (!filesForArchivate.empty())
		fileBackUpInfo << endl << endl << L"Upload files:";
	for (auto iFileName : filesForArchivate)
	{
		fileBackUpInfo << endl << iFileName;
		iFileName = w_targetDirectory + iFileName;
		outputData.push_back(iFileName);
	};
	if (!filesForUpdate.empty())
		fileBackUpInfo << endl << endl << L"Update files:";
	for (auto iFileName : filesForUpdate)
	{
		fileBackUpInfo << endl << iFileName;
		iFileName = w_targetDirectory + iFileName;
		outputData.push_back(iFileName);
	};
	for (auto iFileName : filesForSave)
		outputData.push_back(iFileName);

	fileBackUpInfo.close();
	DeleteFileA(archiveDirectory.c_str());//delete old archive
	Bit7zLibrary lib{ L"7z.dll" };
	BitCompressor compressor{ lib, BitFormat::Zip };
	//compressor.setUpdateMode(true);
	if (!outputData.empty())
		compressor.compressFiles(outputData, w_archiveDirectory);
};

void CleanUp(void)
{
	char* tempPath = new char[w_tempDirectory.length()+4];
	wcstombs(tempPath, w_tempDirectory.c_str(), w_tempDirectory.length());
	tempPath[w_tempDirectory.length()] = '*';
	tempPath[w_tempDirectory.length() + 1] = '.';
	tempPath[w_tempDirectory.length() + 2] = '*';
	tempPath[w_tempDirectory.length() + 3] = '\0';
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	hFind = FindFirstFileA(tempPath, &findFileData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return;
	};
	do
	{
		if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			char* fileName = new char[w_tempDirectory.length()+strlen(findFileData.cFileName)+1];
			wcstombs(fileName, w_tempDirectory.c_str(), w_tempDirectory.length());
			fileName[w_tempDirectory.length()] = '\0';
			strcat(fileName, findFileData.cFileName);
			fileName[w_tempDirectory.length() + strlen(findFileData.cFileName)] = '\0';
			DeleteFileA(fileName);
		};
	} while (0 != FindNextFileA(hFind, &findFileData));
	FindClose(hFind);
	char* tempDirPath = new char[w_tempDirectory.length()];
	wcstombs(tempDirPath, w_tempDirectory.c_str(), w_tempDirectory.length());
	tempDirPath[w_tempDirectory.length() - 1] = '\0';
	RemoveDirectoryA(tempDirPath);
}

void MakeBackup(std::list<std::string> lsFileNames)
{
	std::vector<std::wstring> filesForArchivate, filesForUpdate, filesForSave;

	w_archiveDirectory = new wchar_t[archiveDirectory.length()];
	mbstowcs(w_archiveDirectory, (char*)archiveDirectory.c_str(), archiveDirectory.length());
	w_archiveDirectory[archiveDirectory.length()] = L'\0';
	w_targetDirectory = new wchar_t[targetDirectory.length()];
	mbstowcs(w_targetDirectory, (char*)targetDirectory.c_str(), targetDirectory.length());
	w_targetDirectory[targetDirectory.length()] = L'\0';

	std::ifstream testStream(archiveDirectory);
	if (testStream.fail())
	{
		testStream.close();
		Bit7zLibrary lib{ L"7z.dll" };
		BitCompressor compressor{ lib, BitFormat::Zip };
		for (auto iStr : lsFileNames)
		{
			WCHAR* w_iStr = new wchar_t[iStr.length()];
			mbstowcs(w_iStr, (char*)iStr.c_str(), iStr.length());
			w_iStr[iStr.length()] = L'\0';
			filesForArchivate.push_back(w_iStr);
		};
		CompressIntoNewArchive(filesForArchivate, filesForUpdate, filesForSave);
	}
	else
	{
		testStream.close();
		MakeVectorsForCompress(lsFileNames, filesForArchivate, filesForUpdate, filesForSave);
		if(!filesForSave.empty())
			ExtractFilesInTemp(&filesForSave);
		CompressIntoNewArchive(filesForArchivate, filesForUpdate, filesForSave);
		CleanUp();
	};
	AddLogMessage(0, "Backup! See more information in backupinfo.txt");
}