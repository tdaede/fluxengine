#include "globals.h"
#include "decoders/decoders.h"
#include "encoders/encoders.h"
#include "brother.h"
#include "crc.h"
#include "writer.h"
#include "image.h"
#include "arch/brother/brother.pb.h"
#include "lib/encoders/encoders.pb.h"

static int encode_header_gcr(uint16_t word)
{
	switch (word)
	{
		#define GCR_ENTRY(gcr, data) \
			case data: return gcr;
		#include "header_gcr.h"
		#undef GCR_ENTRY
	}                       
	return -1;             
}

static int encode_data_gcr(uint8_t data)
{
	switch (data)
	{
		#define GCR_ENTRY(gcr, data) \
			case data: return gcr;
		#include "data_gcr.h"
		#undef GCR_ENTRY
	}                       
	return -1;             
}

static void write_bits(std::vector<bool>& bits, unsigned& cursor, uint32_t data, int width)
{
	cursor += width;
	for (int i=0; i<width; i++)
	{
		unsigned pos = cursor - i - 1;
		if (pos < bits.size())
			bits[pos] = data & 1;
		data >>= 1;
	}
}

static void write_sector_header(std::vector<bool>& bits, unsigned& cursor,
		int track, int sector)
{
	write_bits(bits, cursor, 0xffffffff, 31);
	write_bits(bits, cursor, BROTHER_SECTOR_RECORD, 32);
	write_bits(bits, cursor, encode_header_gcr(track), 16);
	write_bits(bits, cursor, encode_header_gcr(sector), 16);
	write_bits(bits, cursor, encode_header_gcr(0x2f), 16);
}

static void write_sector_data(std::vector<bool>& bits, unsigned& cursor, const Bytes& data)
{
	write_bits(bits, cursor, 0xffffffff, 32);
	write_bits(bits, cursor, BROTHER_DATA_RECORD, 32);

	uint16_t fifo = 0;
	int width = 0;

	if (data.size() != BROTHER_DATA_RECORD_PAYLOAD)
		Error() << "unsupported sector size";

	auto write_byte = [&](uint8_t byte)
	{
		fifo |= (byte << (8 - width));
		width += 8;

		while (width >= 5)
		{
			uint8_t quintet = fifo >> 11;
			fifo <<= 5;
			width -= 5;

			write_bits(bits, cursor, encode_data_gcr(quintet), 8);
		}
	};

	for (uint8_t byte : data)
		write_byte(byte);

	uint32_t realCrc = crcbrother(data);
	write_byte(realCrc>>16);
	write_byte(realCrc>>8);
	write_byte(realCrc);
	write_byte(0x58); /* magic */
    write_byte(0xd4);
    while (width != 0)
        write_byte(0);
}

static int charToInt(char c)
{
	if (isdigit(c))
		return c - '0';
	return 10 + tolower(c) - 'a';
}

class BrotherEncoder : public AbstractEncoder
{
public:
	BrotherEncoder(const EncoderProto& config):
		AbstractEncoder(config),
		_config(config.brother())
	{}

public:
	std::vector<std::shared_ptr<Sector>> collectSectors(int physicalTrack, int physicalSide, const Image& image) override
	{
		std::vector<std::shared_ptr<Sector>> sectors;

		int logicalTrack;
		if (physicalSide != 0)
			return sectors;
		physicalTrack -= _config.bias();
		switch (_config.format())
		{
			case BROTHER120:
				if ((physicalTrack < 0) || (physicalTrack >= (BROTHER_TRACKS_PER_120KB_DISK*2))
						|| (physicalTrack & 1))
					return sectors;
				logicalTrack = physicalTrack/2;
				break;

			case BROTHER240:
				if ((physicalTrack < 0) || (physicalTrack >= BROTHER_TRACKS_PER_240KB_DISK))
					return sectors;
				logicalTrack = physicalTrack;
				break;
		}

        for (int sectorId=0; sectorId<BROTHER_SECTORS_PER_TRACK; sectorId++)
        {
            const auto& sector = image.get(logicalTrack, 0, sectorId);
            if (sector)
                sectors.push_back(sector);
		}

		return sectors;
	}

    std::unique_ptr<Fluxmap> encode(int physicalTrack, int physicalSide,
			const std::vector<std::shared_ptr<Sector>>& sectors, const Image& image)
	{
		int logicalTrack;
		if (physicalSide != 0)
			return std::unique_ptr<Fluxmap>();
		physicalTrack -= _config.bias();
		switch (_config.format())
		{
			case BROTHER120:
				if ((physicalTrack < 0) || (physicalTrack >= (BROTHER_TRACKS_PER_120KB_DISK*2))
						|| (physicalTrack & 1))
					return std::unique_ptr<Fluxmap>();
				logicalTrack = physicalTrack/2;
				break;

			case BROTHER240:
				if ((physicalTrack < 0) || (physicalTrack >= BROTHER_TRACKS_PER_240KB_DISK))
					return std::unique_ptr<Fluxmap>();
				logicalTrack = physicalTrack;
				break;
		}

		int bitsPerRevolution = 200000.0 / _config.clock_rate_us();
		const std::string& skew = _config.sector_skew();
		std::vector<bool> bits(bitsPerRevolution);
		unsigned cursor = 0;

		for (int sectorCount=0; sectorCount<BROTHER_SECTORS_PER_TRACK; sectorCount++)
		{
			int sectorId = charToInt(skew.at(sectorCount));
			double headerMs = _config.post_index_gap_ms() + sectorCount*_config.sector_spacing_ms();
			unsigned headerCursor = headerMs*1e3 / _config.clock_rate_us();
			double dataMs = headerMs + _config.post_header_spacing_ms();
			unsigned dataCursor = dataMs*1e3 / _config.clock_rate_us();

			const auto& sectorData = image.get(logicalTrack, 0, sectorId);

			fillBitmapTo(bits, cursor, headerCursor, { true, false });
			write_sector_header(bits, cursor, logicalTrack, sectorId);
			fillBitmapTo(bits, cursor, dataCursor, { true, false });
			write_sector_data(bits, cursor, sectorData->data);
		}

		if (cursor >= bits.size())
			Error() << "track data overrun";
		fillBitmapTo(bits, cursor, bits.size(), { true, false });

		// The pre-index gap is not normally reported.
		// std::cerr << "pre-index gap " << 200.0 - (double)cursor*clockRateUs/1e3 << std::endl;
		
		std::unique_ptr<Fluxmap> fluxmap(new Fluxmap);
		fluxmap->appendBits(bits, _config.clock_rate_us()*1e3);
		return fluxmap;
	}

private:
	const BrotherEncoderProto& _config;

};

std::unique_ptr<AbstractEncoder> createBrotherEncoder(const EncoderProto& config)
{
	return std::unique_ptr<AbstractEncoder>(new BrotherEncoder(config));
}


