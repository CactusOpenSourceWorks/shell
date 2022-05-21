#ifndef CTRL
#define CTRL(c) ((c) & 037)
#endif

#pragma comment (linker, "/defaultlib:ntdll.lib")

#include <conio.h>
#include <cstdio>
#include <ctime>
#include <cstdint>

// https://en.cppreference.com/w/cpp/types/integer
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <winternl.h>
#include <initguid.h>
#include <diskguid.h>

static std::string input;

struct FileRecord {
public:
  std::string name;
  std::string attribute;
  bool directory;
  uint64_t size;
  
  FileRecord(const char *name, uint64_t size, bool directory) : name(name), size(size), directory(directory) {}
  ~FileRecord() {}
};

struct ConsoleCharacter {
  char c;
  WORD a;
};

void ConsolePrint(const char *formatter, ...)
{
  va_list args;
  va_start(args, formatter);
  
  vfprintf(stdout, formatter, args);
  
  va_end(args);
}

void ConsolePrintFast(const char *string)
{
  WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), (const void *) string, 1, nullptr, nullptr);
}

using VectorString = std::vector<std::string>;
using VectorFileRecord = std::vector<FileRecord>;

std::unordered_map<uint16_t, const char *> ImageFileHeader_Characteristics {
  { IMAGE_FILE_RELOCS_STRIPPED, "IMAGE_FILE_RELOCS_STRIPPED" },
  { IMAGE_FILE_EXECUTABLE_IMAGE, "IMAGE_FILE_EXECUTABLE_IMAGE" },
  { IMAGE_FILE_LINE_NUMS_STRIPPED, "IMAGE_FILE_LINE_NUMS_STRIPPED" },
  { IMAGE_FILE_LOCAL_SYMS_STRIPPED, "IMAGE_FILE_LOCAL_SYMS_STRIPPED" },
  // { IMAGE_FILE_AGGRESIVE_WS_TRIM, "IMAGE_FILE_AGGRESIVE_WS_TRIM" }, // Obsolete
  { IMAGE_FILE_LARGE_ADDRESS_AWARE, "IMAGE_FILE_LARGE_ADDRESS_AWARE" },
  // { IMAGE_FILE_BYTES_REVERSED_LO, "IMAGE_FILE_BYTES_REVERSED_LO" }, // Obsolete
  { IMAGE_FILE_32BIT_MACHINE, "IMAGE_FILE_32BIT_MACHINE" },
  { IMAGE_FILE_DEBUG_STRIPPED, "IMAGE_FILE_DEBUG_STRIPPED" },
  { IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP, "IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP" },
  { IMAGE_FILE_NET_RUN_FROM_SWAP, "IMAGE_FILE_NET_RUN_FROM_SWAP" },
  { IMAGE_FILE_SYSTEM, "IMAGE_FILE_SYSTEM" },
  { IMAGE_FILE_DLL, "IMAGE_FILE_DL" },
  { IMAGE_FILE_UP_SYSTEM_ONLY, "IMAGE_FILE_UP_SYSTEM_ONLY" },
  // { IMAGE_FILE_BYTES_REVERSED_HI, "IMAGE_FILE_BYTES_REVERSED_HI" }, // Obsolete
};

std::unordered_map<uint16_t, const char *> ImageFileHeader_Machine {
  { IMAGE_FILE_MACHINE_AMD64, "IMAGE_FILE_MACHINE_AMD64" }, // 0x8664
  { IMAGE_FILE_MACHINE_I386, "IMAGE_FILE_MACHINE_I386" }, // 0x014c
  { IMAGE_FILE_MACHINE_IA64, "IMAGE_FILE_MACHINE_IA64" }, // 0x0200
};

std::unordered_map<uint16_t, const char *> ImageOptionalHeader_DllCharacteristics {
  // Reserved.
};

std::function<void(void)> UpArrowCallFunction = [] (void) -> void {};
std::function<void(void)> DownArrowCallFunction = [] (void) -> void {};
std::function<void(void)> LeftArrowCallFunction = [] (void) -> void {};
std::function<void(void)> RightArrowCallFunction = [] (void) -> void {};
std::function<void(void)> TabCallFunction = [] (void) -> void {};
std::function<void(void)> ExitFunction = [] (void) -> void {};

void Cleanup_HANDLE(HANDLE *h)
{
  if (*h) {
    CloseHandle(*h);
    *h = nullptr;
  }
}

void Cleanup_FILE(FILE **f)
{
  if (*f) {
    fclose(*f);
    *f = nullptr;
  }
}

static uint64_t FileSize(const char *name)
{
  // HANDLE __attribute__((cleanup(Cleanup_HANDLE))) file = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  HANDLE file = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) {
	  CloseHandle(file);
	  return -1;
  }
  
  LARGE_INTEGER size;
  if (!GetFileSizeEx(file, &size)) {
	  CloseHandle(file);
	  return -1;
  }

  CloseHandle(file);
  return size.QuadPart;
}

static std::string GetWorkingDirectory()
{
  const DWORD length = 1024;
  char buffer[length];
  
  GetCurrentDirectory(length, buffer);
  
  return std::string(buffer);
}

static std::string GetUserPrompt()
{
  DWORD length = 256;
  char user[256];
  
  GetUserNameA(user, &length);
  
  std::string username = std::string(user);
  
  DWORD l2 = MAX_COMPUTERNAME_LENGTH + 1;
  char host[MAX_COMPUTERNAME_LENGTH + 1];
  
  GetComputerNameA(host, &l2);
  
  std::string hostname = std::string(host);
  
  return std::string("Cat") + '@' + "CatPC" + " [" + GetWorkingDirectory() + "] ";
  // return hostname;
}

static void PrintPrompt()
{
  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),  6);
  ConsolePrint("%s", GetUserPrompt().c_str());
  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),  7);
}

class Format {
protected:
  uint64_t size = -1;
  bool ready = false;
public:

  Format() {}
  ~Format() {}
  
  uint64_t GetSize() const
  {
    return size;
  }
  
  bool IsReady() const
  {
    return ready;
  }
  
  virtual void Parse(const char *) = 0;
};

class Format_Document : public Format {
public:
  Format_Document() {}
  ~Format_Document() {}
  virtual void Parse(const char *) = 0;
};

class Format_Binary_Image : public Format {
protected:
  uint16_t architecture;
public:
  Format_Binary_Image() {}
  ~Format_Binary_Image() {}
  virtual void Parse(const char *) = 0;
  virtual bool Is64Bit() const = 0;
};

class Format_Archiver : public Format {
public:
  Format_Archiver() {}
  ~Format_Archiver() {}
  virtual void Parse(const char *) = 0;
};

class Format_Image : public Format {
protected:
  uint64_t width, height;
  uint16_t planes;
  
public:
  Format_Image() {}
  ~Format_Image() {}
  
  virtual void Parse(const char *) = 0;
  
  uint64_t GetWidth() const
  {
    return width;
  }
  
  uint64_t GetHeight() const
  {
    return height;
  }
  
  uint16_t GetPlaneCount() const
  {
    return planes;
  }
};

class Format_PDF : public Format_Document {
  std::string version;
public:
  Format_PDF() {}
  ~Format_PDF() {}
  
  std::string GetVersion() const
  {
    return version;
  }
  
  void Parse(const char *name)
  {
    if (IsReady())
      return;
    
    // FILE __attribute__((cleanup(Cleanup_FILE))) *f = fopen(name, "rb");
    FILE *f = fopen(name, "rb");
    if (!f) {
      ready = false;
      return;
    }
    
    char magic[4];
    fread(magic, 4, 1, f);
    
    if (magic[0] == '%' && magic[1] == 'P' && magic[2] == 'D' && magic[3] == 'F') {
      fseek(f, 1, SEEK_CUR);
      char version[16];
      if (fgets(version, 16, f) != nullptr) {
        this->version = version;
      } else {
        this->ready = false;
        return;
      }
    }
    
    this->size = FileSize(name);
    this->ready = true;

	fclose(f);
  }
};

class Format_BMP : public Format_Image {
  uint16_t bpp;
public:
  Format_BMP() {}
  ~Format_BMP() {}
  
  uint16_t GetBPP()
  {
    return bpp;
  }
  
  void Parse(const char *name)
  {
    if (IsReady())
      return;
    
    // FILE __attribute__((cleanup(Cleanup_FILE))) *f = fopen(name, "rb");
    FILE *f = fopen(name, "rb");
    if (!f) {
      ready = false;
      return;
    }
    
    // Header structure (14 bytes, 2 for magic, 4 for file size, 4 reserved, 4 for buffer offset)
    {
      char magic[2];
      fread(magic, 2, 1, f);
      
      if (magic[0] != 'B' || magic[1] != 'M') {
        ready = false;
        return;
      }
      
      fread(&this->size, sizeof(uint32_t), 1, f);
      
      // Skips the last two fields
      fseek(f, 4 * 2, SEEK_CUR);
    }
    
    // Info header structure (40 bytes)
    {
      fseek(f, 4, SEEK_CUR);    
      fread(&this->width, sizeof(uint32_t), 1, f);
      fread(&this->height, sizeof(uint32_t), 1, f);
      fread(&this->planes, sizeof(uint16_t), 1, f);
      fread(&this->bpp, sizeof(uint16_t), 1, f);
    }

	fclose(f);
  }
};

class Format_PE : public Format_Binary_Image {
  std::vector<const char *> properties;
  uint8_t version[2];
  uint16_t version_OS[2];
  uint16_t version_image[2];
  uint64_t size_stack;
  uint32_t checksum;
  uint16_t sections;
public:
  Format_PE() {}
  ~Format_PE() {}
  
  std::vector<const char *> GetProperties() const { return properties; }
  
  uint8_t GetMajorLinkerVersion() const { return version[0]; }
  uint8_t GetMinorLinkerVersion() const { return version[1]; }
  uint16_t GetMajorOSVersion() const { return version_OS[0]; }
  uint16_t GetMinorOSVersion() const { return version_OS[1]; }
  uint16_t GetMajorImageVersion() const { return version_image[0]; }
  uint16_t GetMinorImageVersion() const { return version_image[1]; }
  uint64_t GetStackSize() const { return size_stack; }
  uint32_t GetChecksum() const { return checksum; }
  uint16_t GetSectionCount() const { return sections; }
  
  bool Is64Bit() const
  {
    return (std::find(properties.begin(), properties.end(), "IMAGE_FILE_MACHINE_AMD64") != properties.end()) && (this->architecture == 0x20b);
  }
  
  void Parse(const char *name)
  {
    if (IsReady())
      return;
    
    // FILE __attribute__((cleanup(Cleanup_FILE))) *f = fopen(name, "rb");
    FILE *f = fopen(name, "rb");
    if (!f) {
      ready = false;
      return;
    }
    
    char magic[4];
    fread(magic, 2, 1, f);
    
    // DOS stub is present, which starts with "MZ". Since we've read past the magic identifier, subtract two bytes
    if (magic[0] == 'M' && magic[1] == 'Z') {
      fseek(f, 0x80 - 0x2, SEEK_CUR);
      fread(magic, 4, 1, f);
    }
    
    if (magic[0] != 'P' || magic[1] != 'E' || magic[2] != '\0' || magic[3] != '\0') {
      ready = false;
      return;
    }
    
    // Begin of _IMAGE_FILE_HEADER
    // Read Machine
    uint16_t type;
    fread(&type, 2, 1, f);
    this->properties.push_back(ImageFileHeader_Machine[type]);
    
    // Read NumberOfSections
    fread(&this->sections, 2, 1, f);
    
    // Read TimeDateStamp 
    uint32_t date;
    fread(&date, 4, 1, f);
    
    // Skip over PointerToSymbolTable + NumberOfSymbols (both 4 bytes each)
    fseek(f, 8, SEEK_CUR);
    
    // Read SizeOfOptionalHeader
    uint16_t opt_head_size;
    fread(&opt_head_size, 2, 1, f);
    
    // Read Characteristics
    uint16_t properties;
    fread(&properties, 2, 1, f);
    
    for (const auto &c : ImageFileHeader_Characteristics) {
      if (properties & c.first)
        this->properties.push_back(c.second);
    }
    
    // Optional Header Standard Fields (Image Only)
    fread(&this->architecture, 2, 1, f);
    
    fread(&this->version[0], 1, 1, f);
    fread(&this->version[1], 1, 1, f);
    
    fseek(f,
      4 + /*SizeOfCode */
      4 + /*SizeOfInitializedData */
      4 + /*SizeOfUninitializedData*/
      4 + /*AddressOfEntryPoint */
      4, /*BaseOfCode*/
      SEEK_CUR
    );
    
    if (this->architecture == 0x10b) {
      fseek(f, 4, SEEK_CUR); /* BaseOfData*/
    }
    
    DWORD step = (this->architecture == 0x10b) ? 4 : 8;
    
    fseek(f, 
      step + /* ImageBase */
      (4 * 2), /* SectionAlignment + FileAlignment */
      SEEK_CUR
    );
    
    fread(&this->version_OS[0], 2, 1, f);
    fread(&this->version_OS[1], 2, 1, f);
    
    fread(&this->version_image[0], 2, 1, f);
    fread(&this->version_image[1], 2, 1, f);
    
    fseek(f,
      (2 * 2) + /* MajorSubsystemVersion +MinorSubsystemVersion*/
      (4 * 3), /* Win32VersionValue +SizeOfImage +SizeOfHeaders */
      SEEK_CUR
    );
    
    fread(&this->checksum, 4, 1, f);
    
    uint16_t subsystem;
    fread(&subsystem, 2, 1, f);
    if (subsystem >= IMAGE_SUBSYSTEM_NATIVE)
      this->properties.push_back("IMAGE_SUBSYSTEM_NATIVE");
    if (subsystem >= IMAGE_SUBSYSTEM_WINDOWS_GUI)
      this->properties.push_back("IMAGE_SUBSYSTEM_WINDOWS_GUI");
    if (subsystem >= IMAGE_SUBSYSTEM_WINDOWS_CUI)
      this->properties.push_back("IMAGE_SUBSYSTEM_WINDOWS_CUI");
    if (subsystem >= IMAGE_SUBSYSTEM_OS2_CUI)
      this->properties.push_back("IMAGE_SUBSYSTEM_OS2_CUI");
    if (subsystem >= IMAGE_SUBSYSTEM_POSIX_CUI)
      this->properties.push_back("IMAGE_SUBSYSTEM_POSIX_CUI");
    if (subsystem >= IMAGE_SUBSYSTEM_WINDOWS_CE_GUI)
      this->properties.push_back("IMAGE_SUBSYSTEM_WINDOWS_CE_GUI");
    if (subsystem >= IMAGE_SUBSYSTEM_EFI_APPLICATION)
      this->properties.push_back("IMAGE_SUBSYSTEM_EFI_APPLICATION");
    
    fseek(f, 2, SEEK_CUR); /* DllCharacteristics */
    
    fread(&this->size_stack, step, 1, f);
    
    this->size = FileSize(name);
    this->ready = true;

	fclose(f);
  }
};

class Format_ZIP : public Format_Archiver {
protected:
  uint16_t version;
  uint16_t flags;
  uint16_t compression;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
public:
  Format_ZIP() {}
  ~Format_ZIP() {}
  
  uint32_t GetCRC32() const { return crc32; }
  uint16_t GetVersion() const { return version; }
  
  void Parse(const char *name)
  {
    if (IsReady())
      return;
    
    // FILE __attribute__((cleanup(Cleanup_FILE))) *f = fopen(name, "rb");
    FILE *f = fopen(name, "rb");
    if (!f) {
      ready = false;
      return;
    }
    
    char magic[4];
    fread(magic, 4, 1, f);
    
    if (magic[0] != 'P' || magic[1] != 'K' || magic[2] != 0x03 || magic[3] != 0x04) {
      ready = false;
      return;
    }
    
    fread(&this->version, 2, 1, f);
    ConsolePrint("Version = %i\n", this->version);
    
    fread(&this->flags, 2, 1, f);
    fread(&this->compression, 2, 1, f);
    
    fseek(f, 4, SEEK_CUR);
    
    fread(&this->crc32, 4, 1, f);
    
    fread(&this->compressed_size, 4, 1, f);
    fread(&this->uncompressed_size, 4, 1, f);
    
    ConsolePrint("Compressed/Uncompressed: = %i / %i\n", this->compressed_size, this->uncompressed_size);
    
    this->size = FileSize(name);
    this->ready = true;

	fclose(f);
  }
};

Format_BMP ReadBMP(const char *name)
{
  Format_BMP format;
  format.Parse(name);
  
  return format;
}

Format_PDF ReadPDF(const char *name)
{
  Format_PDF format;
  format.Parse(name);
  
  return format;
}

Format_PE ReadPE(const char *name)
{
  Format_PE format;
  format.Parse(name);
  
  return format;
}

Format_ZIP ReadZIP(const char *name)
{
  Format_ZIP format;
  format.Parse(name);
  
  return format;
}

static VectorFileRecord TraverseDirectory(const char *path) {
  VectorFileRecord v;
  
  WIN32_FIND_DATA data;
  HANDLE h = FindFirstFileEx(path, FindExInfoStandard, &data, FindExSearchNameMatch, nullptr, 0);
  
  if (h != INVALID_HANDLE_VALUE) {
    do {
      FileRecord f(
        data.cFileName,
        (data.nFileSizeHigh * (MAXDWORD + 1)) + data.nFileSizeLow,
        data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
      );
      
      f.attribute += data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? 'h' : '-';
      f.attribute += data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? 'd' : '-';
      f.attribute += data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? 's' : '-';
      f.attribute += data.dwFileAttributes & FILE_ATTRIBUTE_READONLY ? 'r' : '-';
      f.attribute += data.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ? 'a' : '-';
      f.attribute += data.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ? 'c' : '-';
      f.attribute += data.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ? 'e' : '-';
      
      v.push_back(f);
    } while(FindNextFile(h, &data) != 0);
    
    auto error = GetLastError();
    
    if (error == ERROR_NO_MORE_FILES) {
      FindClose(h);
    }
  }
  
  return v;
}

VectorString split(const std::string& s, char seperator, std::function<void(std::string &s)> f = [] (std::string &s) -> void {})
{
   VectorString output;

    std::string::size_type prev_pos = 0, pos = 0;

    while((pos = s.find(seperator, pos)) != std::string::npos)
    {
        std::string substring( s.substr(prev_pos, pos-prev_pos) );
        
        f(substring);

        output.push_back(substring);

        prev_pos = ++pos;
    }
    
    std::string last = s.substr(prev_pos, pos-prev_pos);
    f(last);
    output.push_back(last); // Last word

    return output;
}

void ConsoleSetTitle(const char *title)
{
  SetConsoleTitle(title);
}

bool ConsoleSetPosition(int16_t x, int16_t y)
{
  if (x > GetUserPrompt().length() + input.length())
    x = GetUserPrompt().length() + input.length();
  else if (x <= GetUserPrompt().length())
    x = GetUserPrompt().length();
  
  COORD c = {x, y};
  return SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

std::pair<int16_t, int16_t> ConsoleGetPosition()
{
  CONSOLE_SCREEN_BUFFER_INFO screen;
  
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &screen);
  return std::make_pair(screen.dwCursorPosition.X, screen.dwCursorPosition.Y);
}

std::pair<int, int> ConsoleGetSize()
{
  RECT r;
  
  GetWindowRect(GetConsoleWindow(), &r);
  return std::make_pair(r.right - r.left, r.bottom - r.top);
}

void ConsoleSetSize(int w, int h)
{
  HWND console = GetConsoleWindow();
  RECT r;
  
  GetWindowRect(console, &r);
  MoveWindow(console, r.left, r.top, w, h, TRUE);
}

void ConsoleClearLine(int line)
{
  ConsoleSetPosition(0, ConsoleGetSize().second);
  for (int i = 0; i < ConsoleGetSize().first; ++i)
    ConsolePrintFast("\b \b");
}

void ConsoleClear()
{
  static COORD origin  = {0, 0};
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO screen;
  DWORD written;

  GetConsoleScreenBufferInfo(console, &screen);
  FillConsoleOutputCharacterA(console, ' ', screen.dwSize.X * screen.dwSize.Y, origin, &written);
  FillConsoleOutputAttribute(console, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE, screen.dwSize.X * screen.dwSize.Y, origin, &written);
  SetConsoleCursorPosition(console, origin);
}

void ConsoleWriteCharacter(uint16_t x, uint16_t y, char c)
{
  ConsoleSetPosition(x, y);
  ConsolePrintFast(&c);
}

ConsoleCharacter ConsoleGetCharacterAt(int x, int y)
{
  static COORD block = {1, 1};
  
  ConsoleCharacter cc;
  CHAR_INFO ci;
  COORD coord = {0, 0};
  SMALL_RECT rect = {x, y, x, y};
  
  cc.c = ReadConsoleOutput(GetStdHandle(STD_OUTPUT_HANDLE), &ci, block, coord, &rect) ? ci.Char.AsciiChar : '\0';
  cc.a = ci.Attributes;
  
  return cc;
}

void ConsoleEraseCharacter(uint16_t x, uint16_t y)
{
  auto position = ConsoleGetPosition();
  auto str_pos = ConsoleGetPosition().first - GetUserPrompt().length();
  // ConsolePrint("\n%c\n", input[str_pos - 1]);
  
  for (auto start = str_pos - 1; start <= input.length() - 1; ++start) {
    input[start] = input[start + 1];
    ConsoleWriteCharacter(start + GetUserPrompt().length(), ConsoleGetPosition().second, input[start + 1]);
  }
  
  input.pop_back();
  ConsoleSetPosition(position.first - 1, position.second);
}

struct DriveInfo {
public:
  bool GPT;
  bool MBR_Boot;
  std::string GPT_Type;
  std::string FS;
  uint32_t number;
  uint64_t offset;
  uint64_t size;
  uint64_t sectors;
  unsigned disk;
  std::string name;
};

struct DeviceInfo {
public:
  bool valid;
  bool SSD;
  uint32_t sector_size;
  uint64_t cylinders;
  uint64_t sectors;
  uint16_t number;
  std::vector<DriveInfo> drive;
};

FILE *DeviceToFileHandle(HANDLE h)
{
  int handle = _open_osfhandle((intptr_t) h, _O_RDONLY);
  
  if (handle != -1) {
    return _fdopen(handle, "rb");
  }
  
  CloseHandle(h);
  return nullptr;
}

HANDLE FileHandleToDevice(FILE *f)
{
  if (f)
    return (HANDLE) _get_osfhandle(_fileno(f));
  
  return nullptr;
}

#define IOCTL_VOLUME_BASE   ((DWORD) 'V')
#define IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS CTL_CODE(IOCTL_VOLUME_BASE, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)

static /* unsigned  */ void GetPartitionParentDevice(DriveInfo *info, HANDLE h)
{
  VOLUME_DISK_EXTENTS volumeDiskExtents;
  DWORD dwBytesReturned = 0;
  BOOL result = FALSE;
  
  if ((result = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &volumeDiskExtents, sizeof(volumeDiskExtents), &dwBytesReturned, nullptr)) == TRUE) {
    for (DWORD n = 0; n < volumeDiskExtents.NumberOfDiskExtents; ++n)
    {
      PDISK_EXTENT pDiskExtent = &volumeDiskExtents.Extents[n];
      info->disk = pDiskExtent->DiskNumber;
    }
  }
}

static void GetPartitionFSType(DriveInfo *info, const char *header) {
  if (header) {
    const auto IsNTFSFS = [&header] () -> bool {
      return header[3] == 'N' && header[4] == 'T' && header[5] == 'F' && header[6] == 'S' && header[7] == ' ' && header[8] == ' ' && header[9] == ' ' && header[10] == ' ';
    };

    const auto IsFATFS = [&header] () -> bool {
      if (header[3] == 'M') {
        if (header[4] == 'S') {
          if (header[5] == 'D') { // MSDOS5.0 magic
            return (header[6] == 'O') && (header[7] == 'S') && (header[8] == '5') && (header[9] == '.') && (header[10] == '0');
          } else if (header[5] == 'W') { // MSWIN4.0/MSWIN4.1 magic
            return (header[6] == 'I') && (header[7] == 'N') && (header[8] == '4') && (header[9] == '.') && ((header[10] == '0') || (header[10] == '1'));
          }
        } 
      }
      
      return false;
    };
    
    const auto IsEXFATFS = [&header] () -> bool {
      return header[3] == 'E' && header[4] == 'X' && header[5] == 'F' && header[6] == 'A' && header[7] == 'T' && header[8] == ' ' && header[9] == ' ' && header[10] == ' ';
    };

    const auto IsAndroidFS = [&header] () -> bool {
      if (header[3] == 'a') {
        if (header[4] == 'n') { // android
          return (header[5] == 'd') && (header[6] == 'r') && (header[7] == 'o') && (header[8] == 'i') && (header[9] == 'd') && (header[10] == ' ');
        }
      }
      
      return false;
    };
    
    if (IsNTFSFS())
      info->FS = "NTFS";
    else if (IsFATFS())
      info->FS = "FAT12/FAT16/FAT32";
    else if (IsEXFATFS())
      info->FS = "exFAT";
    else if (IsAndroidFS())
      info->FS = "Android Volume";
    else
      info->FS = "???";
  }
}

static void GetPartitionFSData_GUID(DriveInfo *info, HANDLE h)
{
  FILE *f = DeviceToFileHandle(h);
  
  if (f) {
    // Get sector size
    DWORD sector_size;
    
    DISK_GEOMETRY surface = {0};
    DWORD junk;
    BOOL result = false;
    
    if ((result = DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &surface, sizeof(surface), &junk, (LPOVERLAPPED) NULL)) == TRUE) {
      sector_size = surface.BytesPerSector;
      // Get other drive info (free space, sectors, etc)
      info->sectors = info->size / sector_size;
      
      // NTFS header
      char *header = new char[sector_size];
      
      fseek(f, 0, SEEK_SET);
      fread(header, sector_size, 1, f);
      
      GetPartitionFSType(info, header);

/*      
      for (int i = 0; i < 16; ++i) {
        fread(header, sector_size, 1, f);
        printf("i = %i, buffer = %s\n", i, header);
      }
      */

	  delete[] header;
    }
  }
}

static void GetPartitionFSData_Letter(DriveInfo *info, HANDLE h)
{
  FILE *f = DeviceToFileHandle(h);
  
  if (f) {
    // Get sector size
    DWORD sector_size;
    GetDiskFreeSpaceA((info->name + "\\").c_str(), nullptr, &sector_size, nullptr, nullptr);
    
    // Get other drive info (free space, sectors, etc)
    ULARGE_INTEGER total;
    GetDiskFreeSpaceExA((info->name + "\\").c_str(), nullptr, &total, nullptr);
    
    info->sectors = info->size / (sector_size > 0 ? sector_size : 1);
    
    // NTFS header
	char *header = new char[sector_size];
    
    fseek(f, 0, SEEK_SET);
    fread(header, sector_size, 1, f);
    
    GetPartitionFSType(info, header);

	delete[] header;
  }
}

static void GetAdditionalPartitionInfo(DriveInfo *info, HANDLE h)
{
  if (h && h != INVALID_HANDLE_VALUE) {
    PARTITION_INFORMATION_EX block;
    NTSTATUS status;
    IO_STATUS_BLOCK iosb;
    
    if (0 <= (status = NtDeviceIoControlFile(h, 0, 0, 0, &iosb, IOCTL_DISK_GET_PARTITION_INFO_EX, 0, 0, &block, sizeof(block)))) {
      switch (block.PartitionStyle)
      {
        case PARTITION_STYLE_MBR: {
          info->GPT = false;
          info->MBR_Boot = block.Mbr.BootIndicator;
          
          break;
        }
        
        case PARTITION_STYLE_GPT: {
          info->GPT = true;
          
          if (IsEqualGUID(block.Gpt.PartitionType, PARTITION_ENTRY_UNUSED_GUID)) {
            info->GPT_Type = "PARTITION_ENTRY_UNUSED_GUID";
          } else if (IsEqualGUID(block.Gpt.PartitionType, PARTITION_MSFT_RECOVERY_GUID)) {
            info->GPT_Type = "PARTITION_MSFT_RECOVERY_GUID";
          } else if (IsEqualGUID(block.Gpt.PartitionType, PARTITION_BASIC_DATA_GUID)) {
            info->GPT_Type = "PARTITION_BASIC_DATA_GUID";
          } else if (IsEqualGUID(block.Gpt.PartitionType, PARTITION_SYSTEM_GUID)) {
            info->GPT_Type = "PARTITION_SYSTEM_GUID";
          }
          
          break;
        }
        
        case PARTITION_STYLE_RAW: {
          /* ?? */
          break;
        }
      }
      
      info->size = block.PartitionLength.QuadPart;
      info->offset = block.StartingOffset.QuadPart;
      info->number = block.PartitionNumber;
    }
  }
}

DriveInfo GetDriveDataFromGUID(const char *GUID)
{
  // CreateFile() doesn't allow the trailing '\\' when opening device(s) as GUID. Strip it out.
  std::string name(GUID);
  if (name[name.length() - 1] == '\\')
    name.pop_back();
  
  DriveInfo info;
  HANDLE handle = CreateFile(name.c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (handle != INVALID_HANDLE_VALUE) {
    // Name
    info.name = name;
    
    // info.size, info.offset, info.number, info.GPT will be available after this call
    GetAdditionalPartitionInfo(&info, handle);
    
    // info.sectors, info.NTFS will be available after this call
    GetPartitionFSData_GUID(&info, handle);
    
    // info.disk will be available after this call
    GetPartitionParentDevice(&info, handle);

    CloseHandle(handle);
  }
  
  return info;
}

DriveInfo GetDriveDataFromLetter(const char *p)
{
  // Turn the raw drive letter query into something CreateFile() can digest
  // (i. e. \\?\Volume{GUID_ID}\ -> \\.\\\?\Volume{GUID_ID})
  auto query = std::string("\\\\.\\") + p;
  query.pop_back();
  
  DriveInfo info;
  HANDLE handle = CreateFile(query.c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (handle != INVALID_HANDLE_VALUE) {
    // Name
    std::string name(query);
    name.erase(0, 4);
    info.name = name;
    
    // info.size, info.offset, info.number, info.GPT will be available after this call
    GetAdditionalPartitionInfo(&info, handle);
    
    // info.sectors, info.NTFS will be available after this call
    GetPartitionFSData_Letter(&info, handle);
    
    // info.disk will be available after this call
    GetPartitionParentDevice(&info, handle);
    
    
    CloseHandle(handle);
  }
  
  return info;
}

DWORD    GetFilePointer   (HANDLE hFile) {
    return SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
}

// Try to extract the "representable" name for the given volume GUID to be used. Returns empty if 
// the volume doesn't have a letter assigned, or it's the system recovery volume.
static std::string ExtractDriveNameFromGUID(const char *name)
{
  DWORD size = 16;
  BOOL success = FALSE;
  uint8_t round = 16;
  
  for (uint8_t i = 0; i < round; ++i) {
    char buffer[16];
    success = GetVolumePathNamesForVolumeNameA(name, buffer, size, nullptr);
    if (success == TRUE) {
      return std::string(buffer);
    } else {
      if (GetLastError() == ERROR_MORE_DATA)
        size *= 2;
    }
  }
  
  return std::string();
}

// Extracts typical device properties from a PhysicalDrive-based query.
static DeviceInfo ExtractDeviceInfoFromQuery(const char *drive)
{
  HANDLE hDevice = INVALID_HANDLE_VALUE;  // handle to the drive to be examined 
  BOOL result   = FALSE;                 // results flag
  DWORD junk     = 0;                     // discard results
  DeviceInfo device;
  
  hDevice = CreateFileA(drive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  
  if (hDevice != INVALID_HANDLE_VALUE) {
    
    // Gather DISK_GEOMETRY data
    {
      DISK_GEOMETRY surface;
      
      if ((result = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &surface, sizeof(surface), &junk, (LPOVERLAPPED) NULL)) == TRUE) {
        device.sector_size = surface.BytesPerSector;
        device.cylinders = surface.Cylinders.QuadPart;
        device.sectors = surface.SectorsPerTrack * surface.TracksPerCylinder * surface.Cylinders.QuadPart;
      }
    }
    
    // Gather device type (TRIM of SSD)
    {
      /*
      STORAGE_PROPERTY_QUERY spqTrim;
      spqTrim.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceTrimProperty;
      spqTrim.QueryType = PropertyStandardQuery;

      uint32_t bytesReturned = 0;
      DEVICE_TRIM_DESCRIPTOR dtd = {0};
      if(::DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &spqTrim, sizeof(spqTrim), &dtd, sizeof(dtd), &bytesReturned, NULL) && bytesReturned == sizeof(dtd))
        device.SSD = dtd.TrimEnabled;
      */
    }
    
    // Deduce GPT/MBR device type
    PARTITION_INFORMATION_EX block;
    NTSTATUS status;
    IO_STATUS_BLOCK iosb;
    bool GPT = false;
    
    if (0 <= (status = NtDeviceIoControlFile(hDevice, 0, 0, 0, &iosb, IOCTL_DISK_GET_PARTITION_INFO_EX, 0, 0, &block, sizeof(block)))) {
      GPT = block.PartitionStyle == PARTITION_STYLE_GPT;
    }
    
    // Gather additional disk data
    FILE *f = DeviceToFileHandle(hDevice);
    if (f)
    {
      fseek(f, 0, SEEK_SET);
      
      if (GPT) {
        // Try to read LBA 0 (protective MBR)
        char *buffer = new char[device.sector_size];
        fread(buffer, device.sector_size, 1, f);
        
        // This is the LBA 0, in that case continue reading more bytes to get to our EFI
        if ((buffer[0] != 'E') || (buffer[1] != 'F') || (buffer[2] != 'I')) {
          fread(buffer, device.sector_size, 1, f);
          printf("Correct GPT for %s %i\n", drive, (buffer[0] == 'E') && (buffer[1] == 'F') && (buffer[2] == 'I') && (buffer[3] == ' ') && (buffer[4] == 'P') && (buffer[5] == 'A') && (buffer[6] == 'R') && (buffer[7] == 'T'));
        }

		delete[] buffer;
      } else { // MBR
        char *buffer = new char[device.sector_size];
        fread(buffer, device.sector_size, 1, f);
        printf("MBR buffer for %s: %s\n", drive, buffer);
        printf("Boot sector correctness %i %i 0x%02hhx\n", buffer[device.sector_size - 2] == 0x55, buffer[device.sector_size - 1] == 0xaa, buffer[device.sector_size - 1]);
        
        char partbuf[16 * 4];
        
        for (int i = 0; i < 4; ++i) {
          printf("Copying 16 bytes from %i into %i\n", 16 * i, 446 + (16 * i));
          //memcpy((void *) partbuf, (void *) buffer[446 + (16*i)], 16);
          memcpy(&partbuf[16*i], &buffer[446 + (16*i)], 16);
        }

		delete[] buffer;
        
        for (int i = 0; i < 4; ++i) {
          auto ID = partbuf[16*i + 4];
          if (ID != 0x00) {
            switch (ID) {
              case 131: { // Fall-through to -125, decimal from signed 2's complement
                
              }
              
              case -125: { // 0x83
                uint32_t offset;
                memcpy(&offset, &partbuf[16*i + 8], 4);
                printf("  Found Generic Linux partition at offset %i\n", offset);
                
                uint8_t bootable;
                memcpy(&bootable, &partbuf[16+i+0], 1);
                printf("    Bootable 0x%02hhx\n", bootable);
                
                /*
                fseek(f, 0, SEEK_SET);
                
                fseek(f, (offset + 128) * device.sector_size, SEEK_CUR);
                printf("Current pointer: %i (%i * %i)\n", ftell(f), offset, device.sector_size);
                char buffer[device.sector_size];
                
                
                printf("%i\n", fread(buffer, device.sector_size, 1, f));
                printf("Device buffer: [%s]\n", buffer);
                printf("Device buffer: [%c]\n", buffer[40]);
                
                for (auto i = 0; i < strlen(buffer); ++i)
                {
                  printf(" %i %c", i, buffer[i]);
                  if (buffer[i] == '_' && buffer[i + 1] == 'B') {
                    printf("MOM i'm on the TV!!!\n");
                  }
                }
                */
                
                // EXT4
                {
                  fseek(f, 0, SEEK_SET);
                   _fseeki64(f, (uint64_t) ((uint64_t)(offset) * (uint64_t)device.sector_size), SEEK_CUR);
                   // printf("%" PRIu64 "\n", _ftelli64(f));
                   _fseeki64(f, 1024L, SEEK_CUR);
                  
                  /*
                  fseek(f, 0, SEEK_SET);
                  uint64_t piece = (offset / 16) * device.sector_size, total = 0;
                  for (int i = 0; i < 16; ++i) {
                    printf("fseek = %i (%" PRIu64 " = %i * %i) \n", _fseeki64(f, piece, SEEK_CUR), offset * device.sector_size, offset, device.sector_size);
                    total += piece;
                    printf("%" PRIu64 "\n", _ftelli64(f));
                  }
                  
                  printf("desired offset: %" PRIu64 ", target offset: %" PRIu64 ", total = %" PRIu64 "\n", (uint64_t) ((uint64_t)(offset) * (uint64_t)device.sector_size), _ftelli64(f), total);
                  */
                  
                  /*
                  for (int i = 0; i < 16; ++i) {
                    printf("seeking to %" PRIu64 ": %i\n", (9437312 * device.sector_size) * i, _fseeki64(f, 9437312 * device.sector_size, SEEK_CUR));
                    printf("%" PRIu64 "\n", _ftelli64(f));
                  }
                  */
                  
                  /*
                  SetFilePointer(hDevice, 0, NULL, FILE_BEGIN);
                  SetFilePointer(hDevice, offset * device.sector_size, nullptr, FILE_CURRENT);
                  printf("%" PRIu64 "\n", GetFilePointer(hDevice));
                  */
                  //SetFilePointer(hDevice, 1024, NULL, FILE_CURRENT);
                  DWORD dr;
                  char *buffer = new char[device.sector_size];
                  if (ReadFile(hDevice, buffer, device.sector_size, &dr, 0) == TRUE) {
                    for (int i = 0; i < device.sector_size; ++i) {
                      if (buffer[i] == 0x53) {
                        if (buffer[i+1] == 239 || buffer[i+1] == -17) {
                          printf("      Found ext4-formatted partition at offset %" PRIu64 " byte %" PRIu64 " (%02hhx%02hhx)\n", offset, (uint64_t) ((uint64_t)(offset) * (uint64_t)device.sector_size), buffer[i], buffer[i+1]);
                        }
                      }
                      //printf("%i 0x%02hhx ", i, buffer[i]);
                    }
                  }
				  delete[] buffer;
                }
                
                // BTRFS
                {
                  SetFilePointer(hDevice, 0, NULL, FILE_BEGIN);
                  SetFilePointer(hDevice, (offset + 128) * device.sector_size, NULL, FILE_CURRENT);
                  
                  char *buffer = new char[device.sector_size];
                  DWORD dr;
                  if (ReadFile(hDevice, buffer, device.sector_size, &dr, 0) == TRUE) {
                    for (int i = 0; i < device.sector_size; ++i) {
                      if (buffer[i] == '_' && buffer[i+1] == 'B') {
                        printf("      Found Btrfs-formatted partition at offset %i byte %i (%i)\n", offset+128, ((offset + 128) * device.sector_size) + i, i);
                      }
                    }
                  }
				  delete[] buffer;
                }
                
                break;
              }
              
              case 0x07: {
                uint32_t offset;
                memcpy(&offset, &partbuf[16*i + 8], 4);
                printf("  Found NTFS-formatted partition at offset %i\n", offset);
                break;
              }
              
              case 0x0b: {
                printf("  Found 32-bit FAT partition\n");
                break;
              }
              
              case 130: {

              }
              
              case -126: {
                uint32_t offset;
                memcpy(&offset, &partbuf[16*i + 8], 4);
                printf("  Found Linux swap partition at offset %i\n", offset);
                break;
              }
              
              default: {
                printf("Partition ID: 0x%02hhx\n", ID);
              }
            }
          }
        }
      }
    }
    
    // GAther additio
    {
      //
    }
    
    device.valid = true;
    CloseHandle(hDevice);
  } else {
    device.valid = false;
  }
  
  return device;
}

// Currently, this routine doesn't list EFI system partition(s) and is pretty slow, in the sense it queries for each and every single 
// available PhysicalDrive, and then read all available volume(s) through FindFirstVolumeA()/FindNextVolumeA()/FindVolumeClose(), determine
// if the volume(s) belongs to the particular PhysicalDrive. So it esentially does (number of PhysicalDrive) * (number of volume(s)) iteration.
std::vector<DeviceInfo> ListDisk()
{
  std::vector<DeviceInfo> v;
  for (int i = 0; i < 16; ++i) {
    // Extract basic information for each PhysicalDrive to be queried
    DeviceInfo device = ExtractDeviceInfoFromQuery((std::string("\\\\.\\PhysicalDrive") + std::to_string(i)).c_str());
    
    printf("Valid? %i\n", device.valid);
    
    if (device.valid)
    {
      // printf("i = %i\n", i);
      /*
      printf("Sector size: %i\n", info.sector_size);
      printf("Size: %" PRIu64 " GiB\n", (info.sectors * info.sector_size) / 1024 / 1024 / 1024);
      */
      
      char buffer[256];
      char drive[256];
      HANDLE h = FindFirstVolumeA(buffer, 256);
      BOOL success = FALSE;
      
      // Query for every single volume, using FindFirstVolumeA()/FindNextVolumeA()/FindVolumeClose(), which returns the volume's HANDLE and its GUID
      if (h != INVALID_HANDLE_VALUE) {
        do {
          // Verify if the GUID is actually in the correct format
          if (buffer[0] == '\\' && buffer[1] == '\\' && buffer[2] == '?' && buffer[3] == '\\' && buffer[strlen(buffer) - 1] == '\\') {
            buffer[strlen(buffer) - 1] = 0;
            buffer[strlen(buffer)] = '\\';
            
            // Try to get the partition's drive letter (F:\, E:\, A:\), etc. from GUID (i. e, \\?\Volume{7b1b4990-1a6a-01d4-c824-8c1d350dea00}\)
            auto drive_letter = ExtractDriveNameFromGUID(buffer);
            
            // This is a named, fully functional, healthy partition
            if (drive_letter != std::string()) {
              auto drive = GetDriveDataFromLetter(drive_letter.c_str());
              
              // If the queried partition matches its parent's device ID, then the particular partition belongs to the parent.
              if (drive.disk == i)
                device.drive.push_back(drive);
              
              // printf("%i\n", GetDriveDataFromLetter(query.c_str()));
              // device.push_back(ExtractDeviceInfoFromQuery(query.c_str()));
            } else {
              // Probably something else, i. e. system partition. In that case, query by GUID, not by its drive letter
              auto drive = GetDriveDataFromGUID(buffer);
              
              if (drive.disk == i)
                device.drive.push_back(drive);
            }
          }
          success = FindNextVolumeA(h, buffer, 256);
        } while (success == TRUE);
        
        device.number = i;
        v.push_back(device);
        
        FindVolumeClose(h);
      }
    }
  }
  
  printf("size = %i\n", v.size());
  return v;
}

int main()
{
  {
    auto BMPFile = ReadBMP("TestBMP.bmp");
    ConsolePrint("BMP Size: %i\n", BMPFile.GetSize());
    ConsolePrint("BMP Width/Height: %ix%i\n", BMPFile.GetWidth(), BMPFile.GetHeight());
    ConsolePrint("BMP Planes: %i\n", BMPFile.GetPlaneCount());
    ConsolePrint("BMP BPP: %i\n", BMPFile.GetBPP());
  }
  
  {
    auto PDFFile = ReadPDF("TestPDF.pdf");
    ConsolePrint("PDF Size: %i\n", PDFFile.GetSize());
    ConsolePrint("PDF Version: %s\n", PDFFile.GetVersion().c_str());
  }
  
  {
    auto PEFile = ReadPE("GetFileSize.exe");
    ConsolePrint("PE Checksum: 0x%02hhx\n", PEFile.GetChecksum());
    ConsolePrint("PE Section Count: %i\n", PEFile.GetSectionCount());
    ConsolePrint("PE 32-bit: %i\n", !PEFile.Is64Bit());
    ConsolePrint("PE Linker Version: %i.%i\n", PEFile.GetMajorLinkerVersion(), PEFile.GetMinorLinkerVersion());
    ConsolePrint("PE Required OS Version: %i.%i\n", PEFile.GetMajorOSVersion(), PEFile.GetMinorOSVersion());
    ConsolePrint("PE Image Version: %i.%i\n", PEFile.GetMajorImageVersion(), PEFile.GetMinorImageVersion());
    ConsolePrint("PE Stack Size: %i KiB\n", PEFile.GetStackSize() / 1024 ^ 2);
    ConsolePrint("PE Properties:\n");
    for (const auto &c : PEFile.GetProperties())
      ConsolePrint("  %s\n", c);
    ConsolePrint("\n");
  }
  
  {
    auto ZIPFile = ReadZIP("TestZIP.zip");
    ConsolePrint("ZIP CRC32: 0x%02hhx\n", ZIPFile.GetCRC32());
  }
  
  const auto PrintDirectory = [] (const char *directory) -> void {
    ConsolePrint("\nDirectory contents of %s\n", directory);
    uint32_t n = 0;
    for (const auto &v : TraverseDirectory(directory)) {
      ConsolePrint("%s %s ", v.attribute.c_str(), v.name.c_str());
      if (!v.directory) {
        ConsolePrint("Size: %i\n", v.size);
      } else {
        ConsolePrint("\n");
      }
      ++n;
    }

    ConsolePrint("%i files.\n", n);
  };
  
  /*
  PrintDirectory("D:\\*.*");
  PrintDirectory("C:\\*.*");
  PrintDirectory("G:\\*.*");
  */
  
  ConsoleClear();
  ConsoleSetTitle("shsh (shershell)");
  // ConsolePrint("w = %i; h = %i\n", ConsoleGetSize().first, ConsoleGetSize().second);
  ConsoleSetSize(800, 600);
  // ConsolePrint("w = %i; h = %i\n", ConsoleGetSize().first, ConsoleGetSize().second);
  SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),  5);
  
  // ConsolePrint("Clang %i.%i.%i on Windows\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
  
  LeftArrowCallFunction = [] (void) -> void {
    auto position = ConsoleGetPosition();
    ConsoleSetPosition(position.first - 1, position.second);
  };
  
  RightArrowCallFunction = [] (void) -> void {
    auto position = ConsoleGetPosition();
    ConsoleSetPosition(position.first + 1, position.second);
  };
  
  UpArrowCallFunction = [] (void) -> void {
    ConsolePrint("Up");
  };
  
  DownArrowCallFunction = [] (void) -> void {
    ConsolePrint("Down");
  };
  
  TabCallFunction = [] (void) -> void {
    ConsolePrint("TAB!");
  };
  
  ExitFunction = [] (void) -> void {
    std::exit(0);
  };
  
  for (;;) {
    PrintPrompt();
    bool should_new_line = false;
    
    do {
      auto c = _getch();
      
      if (c == '\r') {
        // std::getline(std::cin, input);
        if (input.find("dir", 0) != std::string::npos) {
          auto v = split(input, ' ', [] (std::string &s) -> void {
            auto length = s.length();
            if (s[length - 1] != '\\' || s[length - 1] != '/') {
              s += '\\';
            }
            
            s += "*.*";
          });
          
          if (v.size() >= 2) {
            for (int i = 0; i < v.size(); ++i) {
              PrintDirectory(v[i].c_str());
            }
          } else {
            auto dir = GetWorkingDirectory();
            if (dir[dir.length() - 1] != '\\' || dir[dir.length() - 1] != '/') {
              dir += '\\';
            }
            
            dir += "*.*";
            PrintDirectory(dir.c_str());
          }
        } else if ((input.find("clear", 0) != std::string::npos) || (input.find("cls", 0) != std::string::npos)) {
          ConsoleClear();
        } else if (input.find("type", 0) != std::string::npos) {
          auto v = split(input, ' ');
          if(v[1].substr(v[1].find_last_of(".") + 1) == "pdf") {
            auto PDFFile = ReadPDF("TestPDF.pdf");
            ConsolePrint("PDF Size: %i\n", PDFFile.GetSize());
            ConsolePrint("PDF Version: %s\n", PDFFile.GetVersion().c_str());
          } else if (v[1].substr(v[1].find_last_of(".") + 1) == "exe") {
            auto PEFile = ReadPE("GetFileSize.exe");
            ConsolePrint("PE Checksum: 0x%02hhx\n", PEFile.GetChecksum());
            ConsolePrint("PE Section Count: %i\n", PEFile.GetSectionCount());
            ConsolePrint("PE 32-bit: %i\n", !PEFile.Is64Bit());
            ConsolePrint("PE Linker Version: %i.%i\n", PEFile.GetMajorLinkerVersion(), PEFile.GetMinorLinkerVersion());
            ConsolePrint("PE Required OS Version: %i.%i\n", PEFile.GetMajorOSVersion(), PEFile.GetMinorOSVersion());
            ConsolePrint("PE Image Version: %i.%i\n", PEFile.GetMajorImageVersion(), PEFile.GetMinorImageVersion());
            ConsolePrint("PE Stack Size: %i KiB\n", PEFile.GetStackSize() / 1024 ^ 2);
            ConsolePrint("PE Properties:\n");
            for (const auto &c : PEFile.GetProperties())
              ConsolePrint("  %s\n", c);
            ConsolePrint("\n");
          }
        } else if (input == "exit") {
          ExitFunction();
        } else if (input == "list") {
          for (const auto &device : ListDisk()) {
            if (device.valid) {
              printf("ID: #%i\n", device.number);
              printf("SSD: %i\n", device.SSD);
              printf("Cylinders: %i\n", device.cylinders);
              printf("Sector size: %i bytes\n", device.sector_size);
              printf("Size: %" PRIu64 " GiB\n", (device.sectors * device.sector_size) / 1024 / 1024 / 1024);
              for (const auto &drive : device.drive) {
                printf("  Name: %s\n", drive.name.c_str());
                printf("    Parent: #%i\n", drive.disk);
                printf("    Number: #%i\n", drive.number);
                printf("    FS: %s\n", drive.FS.c_str());
                printf("    GPT: %i\n", drive.GPT);
                printf("    Sectors: %i\n", drive.sectors);
                printf("    Starting Offset: %" PRIu64 " \n", drive.offset);
                printf("    Size: %" PRIu64 " bytes (%" PRIu64 " MiB)\n", drive.size, drive.size / 1024 / 1024);
                
                if (drive.GPT) {
                  printf("      GPT type: %s\n", drive.GPT_Type.c_str());
                } else {
                  printf("      MBR boot: %i\n", drive.MBR_Boot);
                }
              }
            }
          }
        } else if (input.find("cd", 0) != std::string::npos) {
          auto v = split(input, ' ');
          SetCurrentDirectory(v[1].c_str());
        }
        ConsolePrint("Input received: %s\n", input.c_str());
        ConsolePrint("\n");
        input = "";
        should_new_line = true;
      } else if (c == '  ') {
        TabCallFunction();
        should_new_line = false;
      } else if (c == '\b') {
        if (ConsoleGetPosition().first > GetUserPrompt().length()) {
          ConsoleEraseCharacter(ConsoleGetPosition().first, ConsoleGetPosition().second);
          should_new_line = false;
        }
      } else if (c == 224) {
        switch(_getch()) { // the real value
          case 'H': {
            DownArrowCallFunction();
            break;
          }
          case 'P': {
            UpArrowCallFunction();
            break;
          }
          case 'M': {
            RightArrowCallFunction();
            break;
          }
          case 'K': {
            LeftArrowCallFunction();
            break;
          }
          case 71: {
            ConsoleSetPosition(GetUserPrompt().length(), ConsoleGetPosition().second);
            break;
          }
          case 79: {
            ConsoleSetPosition(GetUserPrompt().length() + input.length(), ConsoleGetPosition().second);
            break;
          }
        }
      } else if (c == CTRL('c')) {
        ConsolePrint("\n");
        input = "";
        should_new_line = true;
      } else {
        input += c;
        putchar(c);
        should_new_line = false;
      }
    } while(should_new_line == false);
  }
}
