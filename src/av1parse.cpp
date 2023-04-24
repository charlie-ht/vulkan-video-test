#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cassert>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

void print_binary(u8 byte)
{
	for (int bit = 8 - 1; bit >=0; bit--)
		printf("%d", (byte & (1 << bit)) ? 1 : 0);
}

std::vector<u8>
readEntireFile(const std::string& path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		abort();
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<u8> buffer(size);
	if (file.read((std::basic_istream<char>::char_type*)buffer.data(), size))
	{
		return buffer;	
	}

	printf("FAILED TO OPEN THE FILE\n");
	buffer.resize(0);
	return buffer;
}

class Av1Parser
{
public:
	using Ty_ = std::vector<u8>;

	Av1Parser(const Ty_& bitstream) : m_bitstream(bitstream) {}

	u32 leb128()
	{
printf("leb128\n");
		u32 value{0};
		_Leb128Bytes = 0;
		for (int i = 0; i < 8; i++) {
			u32 leb128_byte = f(8);
			value |= ( (leb128_byte & 0x7f) << (i*7) );
			_Leb128Bytes += 1;
			if ( !(leb128_byte & 0x80) ) {
				break;
			}
		}
		// bitstream conformance requires this <= 2*32-1
printf("\t leb128 - returns %d 0x%x from %d bytes\n", value, value, _Leb128Bytes);

		return value;
	}
	u32 _Leb128Bytes{0};

int read_bit() { 
	assert(more_data_in_bitstream());
	u8 curr_byte = m_bitstream[m_offset_bits / 8];
	u8 bit_offset = m_offset_bits % 8;
	print_binary(curr_byte); puts("\n");
	u8 curr_bit  = (curr_byte & (1 << (7 - bit_offset))) ? 1 : 0;
	printf("read a bit %d (%d) of byte %zu (0x%x)\n", bit_offset, curr_bit, m_offset_bits/8, curr_byte, curr_byte);
	m_offset_bits++;
	return curr_bit;
};  

int f(int n) {
printf("f\n");
	int x = 0;
	for (int i = 0; i < n; i++) {
		x = 2 * x + read_bit();
	}
printf("\tf returns %d 0x%x\n", x, x);

	return x;
}
void parse_annexB()
{
	printf("parse_annexB\n");
	while (more_data_in_bitstream())
		temporal_unit(leb128());
}
void parse_ivf()
{
#pragma pack(push,1)
	struct IvfHeader {
		u32 signature;
		u16 version;
		u16 hdr_length_in_bytes;
		u32 fourcc;
		u16 width_in_pixels;
		u16 height_in_pixels;
		u32 time_base_denominator;
		u32 time_base_numerator;
		u32 num_frames;
		u32 unused;
	};
	struct IvfFrameHeader {
		u32 frame_size_in_bytes; // not including header
		u64 presentation_timestamp;
	};
#pragma pack(pop)
	IvfHeader hdr = *(IvfHeader*)data();
	m_offset_bits += sizeof(IvfHeader)*8;
	while (hdr.num_frames > 0) {
		assert(more_data_in_bitstream());
		IvfFrameHeader frame_hdr = *(IvfFrameHeader*)data();
		m_offset_bits += sizeof(IvfFrameHeader)*8;
		m_offset_bits = 0x2e * 8;
		printf("processing frame of size %d bytes offset 0x%lx\n", frame_hdr.frame_size_in_bytes, offset_bytes());
		obu_header();
		m_offset_bits += frame_hdr.frame_size_in_bytes*8;
		hdr.num_frames -= 1;
	}
}
void temporal_unit( int sz )
{
printf("temporal_unit\n");

	while (sz > 0) {
		int frame_unit_size = leb128();
		sz -= _Leb128Bytes;
		frame_unit( frame_unit_size );
		sz -= frame_unit_size;
	}
}
void frame_unit( int sz )
{
printf("frame_unit\n");

	while (sz > 0) {
		int obu_length = leb128();
		sz -= _Leb128Bytes;
		printf("opening obu of size %d\n", obu_length);
		sz -= obu_length;
	}
}

void obu_header()
{
printf("obu_header\n");

	int obu_forbidden_bit = 	f(1);
	assert(obu_forbidden_bit == 0);
	_obu_type = 			f(4);
	_obu_extension_flag = 	f(1);
	_obu_has_size_field = 	f(1);
	int obu_reserved_1bit = 	f(1);
	if (_obu_extension_flag == 1)
		assert(false); // obu_extension_header();
}
int _obu_type{};
int _obu_extension_flag{};
int _obu_has_size_field{};

void trailing_bits(int n_bits)
{
printf("trailing_bits\n");

	assert(n_bits > 0);
	f(1);
	n_bits--;
	while (n_bits > 0) {
		f(1);
		n_bits--;
	}
}
void byte_alignment()
{
	while (get_position() & 7) f(1);
}
int more_data_in_bitstream() { return offset_bytes()  < total_size() ? 1 : 0; }

// Accessors
Ty_::size_type total_size() { return m_bitstream.size(); }
Ty_::size_type offset_bits() { return m_offset_bits; }
Ty_::size_type get_position() { return offset_bits(); }
Ty_::size_type offset_bytes() { return m_offset_bits/8; }
Ty_::size_type remaining_bits() { return total_size()-offset_bytes(); }
Ty_::const_pointer   data() { return m_bitstream.data() + offset_bytes(); }

// Data
const Ty_ m_bitstream;
Ty_::size_type m_offset_bits{0};
};


int main()
{
	auto p = Av1Parser(readEntireFile("test.ivf"));

	printf("bitstream contains %ld 0x%lx bytes\n", p.total_size(), p.total_size());
	p.parse_ivf();
	
	printf("bitstream now contains %ld 0x%lx bytes\n", p.total_size(), p.total_size());

	return 0;
}
