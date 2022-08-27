#ifndef VFS_H
#define VFS_H

#include "lib/bytes.h"

class Sector;
class Image;
class Brother120Proto;
class DfsProto;
class FilesystemProto;
class SectorInterface;

enum FileType
{
    TYPE_FILE,
    TYPE_DIRECTORY
};

struct Dirent
{
    std::string filename;
    FileType file_type;
    uint64_t length;
    std::string mode;
};

enum FilesystemStatus
{
    FS_OK,
    FS_OK_BUT_UNUSED_BAD_SECTORS,
    FS_OK_BUT_USED_BAD_SECTORS,
    FS_MISSING_CRITICAL_SECTORS,
    FS_BAD
};

class FilesystemException
{
public:
    FilesystemException(const std::string& message): message(message) {}

public:
    std::string message;
};

class BadPathException : public FilesystemException
{
public:
    BadPathException(): FilesystemException("Bad path") {}
};

class FileNotFoundException : public FilesystemException
{
public:
    FileNotFoundException(): FilesystemException("File not found") {}
};

class BadFilesystemException : public FilesystemException
{
public:
    BadFilesystemException(): FilesystemException("Invalid filesystem") {}
};

class ReadOnlyFilesystemException : public FilesystemException
{
public:
    ReadOnlyFilesystemException(): FilesystemException("Read only filesystem")
    {
    }
};

class UnimplementedFilesystemException : public FilesystemException
{
public:
    UnimplementedFilesystemException():
        FilesystemException("Unimplemented operation")
    {
    }
};

class Path : public std::vector<std::string>
{
public:
    Path() {}
    Path(const std::string& text);

public:
    std::string to_str() const;
};

class Filesystem
{
public:
    virtual void create()
    {
        throw UnimplementedFilesystemException();
    }

    virtual FilesystemStatus check()
    {
        throw UnimplementedFilesystemException();
    }

    virtual std::vector<std::unique_ptr<Dirent>> list(const Path& path)
    {
        throw UnimplementedFilesystemException();
    }

    virtual Bytes getFile(const Path& path)
    {
        throw UnimplementedFilesystemException();
    }

    virtual void putFile(const Path& path, const Bytes& data)
    {
        throw UnimplementedFilesystemException();
    }

    virtual std::map<std::string, std::string> getMetadata(const Path& path)
    {
        throw UnimplementedFilesystemException();
    }

    virtual void putMetadata(
        const Path& path, const std::map<std::string, std::string>& metadata)
    {
        throw UnimplementedFilesystemException();
    }

protected:
    Filesystem(std::shared_ptr<SectorInterface> sectors);

    Bytes getLogicalSector(uint32_t number, uint32_t count = 1);
    void putLogicalSector(uint32_t number, const Bytes& data);

    unsigned getLogicalSectorCount();
    unsigned getLogicalSectorSize();

private:
    typedef std::tuple<unsigned, unsigned, unsigned> location_t;
    std::vector<location_t> _locations;
    std::shared_ptr<SectorInterface> _sectors;

public:
    static std::unique_ptr<Filesystem> createBrother120Filesystem(
        const FilesystemProto& config, std::shared_ptr<SectorInterface> image);
    static std::unique_ptr<Filesystem> createAcornDfsFilesystem(
        const FilesystemProto& config, std::shared_ptr<SectorInterface> image);
    static std::unique_ptr<Filesystem> createFatFsFilesystem(
        const FilesystemProto& config, std::shared_ptr<SectorInterface> image);

    static std::unique_ptr<Filesystem> createFilesystem(
        const FilesystemProto& config, std::shared_ptr<SectorInterface> image);
};

#endif
