#ifndef READER_H
#define READER_H

class Fluxmap;

class ReaderTrack
{
public:
    virtual ~ReaderTrack() {}

    int track;
    int side;

    std::unique_ptr<Fluxmap> read();
    virtual std::unique_ptr<Fluxmap> reallyRead() = 0;
};

class CapturedReaderTrack : public ReaderTrack
{
public:
    std::unique_ptr<Fluxmap> reallyRead();
};

class FileReaderTrack : public ReaderTrack
{
public:
    std::unique_ptr<Fluxmap> reallyRead();
};

extern void setReaderDefaultSource(const std::string& source);
extern std::vector<std::unique_ptr<ReaderTrack>> readTracks();

#endif
