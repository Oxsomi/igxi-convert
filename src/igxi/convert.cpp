#include "igxi/convert.hpp"
#include "system/system.hpp"
#include "system/log.hpp"
#include "system/local_file_system.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize.h"

using namespace ignis;

namespace igxi {

	//Load a given file and mips

	inline Helper::ErrorMessage load(
		List<Buffer> &/*out*/, u16 baseMip, u16 /*mips*/, const String &path,
		Helper::Flags flags, u16 &/*width*/, u16 &/*height*/, GPUFormat &format,
		const List<Array<u16, 5>> &sizes
	) {

		//Get file

		Buffer file;

		{
			IGXI::File loader(path, false);
			usz start{};
			usz length = loader.size();

			file.resize(length);

			if (loader.readRegion(file.data(), start, length))
				return Helper::INVALID_FILE_PATH;
		}

		if (file.size() >= (usz(1) << (sizeof(int) * 8)))
			return Helper::INVALID_FILE_BOUNDS;

		//Read image via stbi
		//Supports jpg/png/bmp/gif/psd/pic/pnm/hdr/tga
		//Preserve all bit depth

		stbi__result_info ri;

		stbi__context s;
		stbi__start_mem(&s, file.data(), int(file.size()));

		u8 *data{};
		bool inputFloat{}, input16Bit{};

		int x{}, y{}, comp{}, stride = 1;

		const bool is1D = flags & Helper::IS_1D;

		int channelCount;

		switch (flags & Helper::PROPERTY_CHANNELS) {

			case 0: channelCount = 0; break;

			case Helper::IS_R: channelCount = 1; break;
			case Helper::IS_RG: channelCount = 2; break;
			case Helper::IS_RGB: channelCount = 3; break;
			case Helper::IS_RGBA: channelCount = 4; break;

			default: return Helper::INVALID_CHANNELS;

		}

		if (stbi__hdr_test(&s)) {

			data = (u8*) stbi__hdr_load(&s, &x, &y, &comp, channelCount, &ri);
			inputFloat = true;
			stride = 4;

		} else {

			//TODO: Change 16 to 0 and fix it so it assumes the load format
			u8 *intermediate = (u8*) stbi__load_main(&s, &x, &y, &comp, channelCount, &ri, 16);	

			intermediate;

			stride += int(input16Bit = ri.bits_per_channel == 16);
		}

		if (!channelCount)
			channelCount = comp;

		if (!data)
			return Helper::INVALID_FILE_DATA;

		//Convert to correct dimension

		if (is1D) {
			x *= y;
			y = 1;
		}

		//TODO: Initialize sizes here!

		if (x >= sizes[baseMip][1] || y >= sizes[baseMip][2] || x <= 0 || y <= 0) {
			stbi_image_free(data);
			return Helper::INVALID_IMAGE_SIZE;
		}

		//Get primitive

		GPUFormatType primitive;

		switch (flags & Helper::PROPERTY_PRIMTIIVE) {

			case 0: primitive = inputFloat ? GPUFormatType::FLOAT : GPUFormatType::UNORM; break;

			case Helper::IS_SINT: primitive = GPUFormatType::SINT; break;
			case Helper::IS_UINT: primitive = GPUFormatType::UINT; break;
			case Helper::IS_UNORM: primitive = GPUFormatType::UNORM; break;
			case Helper::IS_SNORM: primitive = GPUFormatType::SNORM; break;
			case Helper::IS_FLOAT: primitive = GPUFormatType::FLOAT; break;

			default: return Helper::INVALID_PRIMITIVE;

		}

		//Get bits

		u32 bytes;

		switch (flags & Helper::PROPERTY_BITS) {

			case 0: bytes = inputFloat || input16Bit ? 2 : 1; break;

			case Helper::IS_8_BIT: bytes = 1; break;
			case Helper::IS_16_BIT: bytes = 2; break;
			case Helper::IS_32_BIT: bytes = 4; break;
			case Helper::IS_64_BIT: bytes = 8; break;

			default: return Helper::INVALID_BITS;

		}

		//Get format

		format = GPUFormat::NONE;

		if (flags & Helper::IS_SRGB) {

			if (bytes == 1 && channelCount == 3)
				format = GPUFormat::sRGB8;
			else if (bytes == 2 && channelCount == 4)
				format = GPUFormat::sRGBA8;

		} else {

			if(
				!(bytes == 1 && primitive == GPUFormatType::FLOAT) && 
				!(bytes > 2 && !(u8(primitive) & u8(GPUFormatType::PROPERTY_IS_UNNORMALIZED)))
			)
				format = GPUFormat(u16((channelCount - 1) | ((bytes - 1) << 2) | (u8(primitive) << 4)));

		}

		if (format == GPUFormat::NONE)
			return Helper::INVALID_FORMAT;

		//TODO: Foreach pixel: convert to correct primitive
		//TODO: Foreach pixel: convert to correct channel size
		//TODO: SRGB?
		//TODO: Copy into out[baseMip]

		//Generate mips

		if (flags & Helper::GENERATE_MIPS) {

			//TODO: Only supported with a few formats
			//TODO: Use premultiplied alpha before generating mips
			//		Get it back somehow?

			//TODO: MIP_NEAREST, MIP_LINEAR, MIP_MIN, MIP_LINEAR
			//TODO: stbir filters
			//TODO: Report progress so you can see how much has been converted

			//stbir_resize_float(...);
			//Copy into out[baseMip + n] where n < mips
		}

		stbi_image_free(data);
		return Helper::INVALID_OPERATION;
	}

	//Find all files that correspond with the given path and parse their file description

	/*inline Helper::ErrorMessage findFiles(
		const String &path, Helper::Flags flags, List<String> &files
	) {*/

		//TODO: Find appropriate files
		//Types:
		//
		//	If IS_1D is set, it will read the file as a 1D image (even if 2D)
		//	
		//	If IS_3D is set; see IS_ARRAY but it is interpreted as 3D
		//		If both are set, they are merged into one bigger 3D texture
		//	
		//	If IS_CUBE is set it will load 6 slices:
		//		It will look for suffix (case-insensitive)
		//			'right'	= '+x'
		//			'left' = '-x'
		//			'top' = '+y'
		//			'bottom' = '-y'
		//			'back' = '+z'
		//			'front' = '-z'
		//	
		//		If GENERATE_MIPS isn't set; all mips have to be initialized
		//			There can be a 1 character separator after the cube face suffix
		//			'+x' = mip 0, cube face right
		//			'front4' = 'front_4' = 'front.4' = mip 4, cube face front
		//	
		//		If IS_ARRAY is set; it will require an index
		//			E.g. 'right0', 'right.1', 'top-1', 'bottom_1'
		//			If GENERATE_MIPS is not set, it will require the mip then a seperator and then the arraySlice
		//				E.g. 'right0-1', 'right.0.1', 'top-1_2', etc.
		//	
		//	If IS_ARRAY is set, it will include files with the path name and a number after it
		//		If a number is missing, it ignores it. All numbers are put from lowest to highest
		//

		//MISSING_FACE is if a face of the cube is missing
		//MISSING_MIP is if GENERATE_MIP is off and one of the mips isn't provided
		//MISSING_LAYER is if one of the layers is missing
		//MISSING_SAMPLE is if one of the layers (samples) of the MS texture is missing

		//TODO: return files in order from z, layer, mip
	//}

	//Copy memory from temporary image into our target

	inline Helper::ErrorMessage insertInto(
		Buffer &out, const Buffer &buf, u16 z, u16 layer, const IGXI::Header &/*header*/, const Array<u16, 5> &size
	) {

		if (layer >= size[4] || z >= size[3])
			return Helper::INVALID_RESOURCE_INDEX;

		usz oneImg = usz(size[2]) * size[1] * size[0];

		if(buf.size() != oneImg)
			return Helper::INVALID_IMAGE_SIZE;

		std::memcpy(out.data() + (layer * size[3] + z) * oneImg, buf.data(), oneImg);
		return Helper::SUCCESS;
	}

	inline Helper::ErrorMessage insertInto(
		IGXI &out, const Buffer &buf, u16 format, u16 z, u16 layer, u16 mip, const Array<u16, 5> &size
	) {

		if (format >= out.header.formats || mip >= out.header.mips)
			return Helper::INVALID_RESOURCE_INDEX;

		return insertInto(out.data[format][mip], buf, z, layer, out.header, size);
	}

	//Convert to a valid IGXI file

	Helper::ErrorMessage Helper::convert(IGXI &out, const List<FileDesc> &files, Flags flags) {

		if(files.empty())
			return MISSING_PATHS;

		out = {};

		//Get type

		TextureType type;

		switch (flags & PROPERTY_TYPE) {

			case 0:			type = TextureType::TEXTURE_2D;		break;
			case IS_2D:		type = TextureType::TEXTURE_2D;		break;
			case IS_CUBE:	type = TextureType::TEXTURE_CUBE;	break;
			case IS_1D:		type = TextureType::TEXTURE_1D;		break;
			case IS_3D:		type = TextureType::TEXTURE_3D;		break;

			case IS_MS | IS_2D:
			case IS_MS:		type = TextureType::TEXTURE_MS;		break;

			default:		return INVALID_TYPE;
		}


		if (flags & IS_ARRAY) {
			if (flags & IS_3D)	return INVALID_TYPE;
			else				type = TextureType(u8(type) | u8(TextureType::PROPERTY_IS_ARRAY));
		}

		//Get usage

		GPUMemoryUsage usage{};

		if (flags & MEMORY_SHARED)
			usage = GPUMemoryUsage::SHARED;

		if (flags & MEMORY_PREFER)
			usage |= GPUMemoryUsage::PREFER;

		if (flags & MEMORY_CPU_READ)
			usage |= GPUMemoryUsage::CPU_READ;

		if (flags & MEMORY_CPU_WRITE)
			usage |= GPUMemoryUsage::CPU_WRITE;

		if (flags & MEMORY_GPU_WRITE)
			usage |= GPUMemoryUsage::GPU_WRITE;

		//Get other dimensions than x & y

		u16 length{}, layers{}, mips{};

		for (const FileDesc &desc : files) {

			if (desc.iid.z == 0xFFFF || desc.iid.layer == 0xFFFF || desc.iid.mip == 0xFFF)
				return INVALID_RESOURCE_INDEX;

			length = std::max(length, u16(desc.iid.z + 1));
			layers = std::max(layers, u16(desc.iid.layer + 1));
			mips = std::max(mips, u16(desc.iid.mip + 1));
		}

		//Check for missing resource indices

		if (flags & GENERATE_MIPS && mips != 1)
			return TOO_MANY_MIPS;

		u16 checkMipCount = flags & GENERATE_MIPS ? 1 : mips;

		for (u64 i = 0; i < u64(length) * layers * checkMipCount; ++i) {

			u16 z = u16(i % length);
			u16 l = u16(i / length % layers);
			u16 m = u16(i / length / layers);

			bool contains{};

			for(const FileDesc &desc : files)
				if (desc.iid.mip == m && desc.iid.layer == l && desc.iid.z == z) {
					contains = true;
					break;
				}

			if (!contains)
				return MISSING_RESOURCE_INDEX;
		}

		if (flags & IS_CUBE && layers % 6)
			return MISSING_FACE;

		//Output data

		out.header.flags = IGXI::Flags::CONTAINS_DATA;
		out.header.formats = 1;
		out.header.usage = usage;
		out.header.type = type;
		out.header.length = length;
		out.header.layers = layers;
		out.header.mips = u8(mips);

		//Process files

		bool isFirst = true;

		List<Array<u16, 5>> sizes;

		for (const FileDesc &file : files) {

			List<Buffer> fileData;

			u16 x, y;
			GPUFormat format;

			if (Helper::ErrorMessage msg = load(
				fileData, file.iid.mip, flags & GENERATE_MIPS ? mips : 1, file.path, flags, x, y, format, sizes
			))
				return msg;

			if (isFirst) {

				isFirst = false;
				out.header.width = x;
				out.header.height = y;

				out.format = { format };
				out.data.resize(1);
				out.data[0].resize(mips);

				u16 mip{};

				u16 stride = u16(FormatHelper::getSizeBytes(format));
				u16 z = length;

				for (Buffer &b : out.data[0]) {

					b.resize(layers * z * y * x * stride);

					sizes[mip] = { stride, x, y, z, layers };

					z = u16(std::ceil(f64(z) / 2));
					y = u16(std::ceil(f64(y) / 2));
					x = u16(std::ceil(f64(x) / 2));
					++mip;
				}

			} else if (x != out.header.width || y != out.header.height)
				return CONFLICTING_IMAGE_SIZE;

			else if(format != out.format[0])
				return CONFLICTING_IMAGE_FORMAT;

			u16 mip{};

			for (const Buffer &buf : fileData)
				if (ErrorMessage msg = insertInto(out, buf, 0, file.iid.z, file.iid.layer, file.iid.mip + mip, sizes[mip]))
					return msg;
				else 
					++mip;
		}

		//Compress

		if (flags & DO_COMPRESSION) {
			//TODO: Set format to compressed formats
			//TODO: Convert everytime a FULL layer is added to minimize memory usage
		}

		return SUCCESS;
	}

	//Parse descs by paths

	//TODO: Doesn't work yet! Implement

	static Helper::ErrorMessage findSide(const String&, Helper::Flags, u16&) { return Helper::INVALID_OPERATION; }
	static Helper::ErrorMessage findSample(const String&, Helper::Flags, u16&) { return Helper::INVALID_OPERATION; }
	static Helper::ErrorMessage findZ(const String&, Helper::Flags, u16&) { return Helper::INVALID_OPERATION; }
	static Helper::ErrorMessage findSlice(const String&, Helper::Flags, u16&) { return Helper::INVALID_OPERATION; }
	static Helper::ErrorMessage findMip(const String&, Helper::Flags, u8&) { return Helper::INVALID_OPERATION; }

	static Helper::ErrorMessage findFiles(const String&, Helper::Flags, List<String>&) { return Helper::INVALID_OPERATION; }

	Helper::ErrorMessage Helper::convert(IGXI &out, const List<String> &paths, Flags flags) {

		usz j = paths.size();
		List<FileDesc> files(j);

		//usz sliceMultiplier = flags & IS_CUBE ? 6 : 1;

		enum Index : u8 { SIDE, SAMPLE, SLICE, SIZE };

		using Indices = Array<u16, SIZE>;

		List<Indices> indices(j);
		Indices max{};

		for (usz i = 0; i < j; ++i) {

			FileDesc &f = files[i];
			f.path = paths[i];

			auto &index = indices[i];

			if (flags & IS_CUBE)
				if (ErrorMessage msg = findSide(f.path, flags, index[SIDE]))
					return msg;

			if (flags & IS_MS)
				if (ErrorMessage msg = findSample(f.path, flags, index[SAMPLE]))
					return msg;

			if (flags & IS_3D)
				if (ErrorMessage msg = findZ(f.path, flags, f.iid.z))
					return msg;

			if (flags & IS_ARRAY)
				if (ErrorMessage msg = findSlice(f.path, flags, index[SLICE]))
					return msg;

			if (!(flags & GENERATE_MIPS))
				if (ErrorMessage msg = findMip(f.path, flags, f.iid.mip))
					return msg;

			for (u8 k = 0; k < SIZE; ++k)
				if (index[k] == 0xFFFF)
					return INVALID_RESOURCE_INDEX;
				else
					max[k] = std::max(u16(index[k] + 1), max[k]);
		}

		for (usz i = 0; i < j; ++i) {

			auto &idx = indices[i];

			auto layer = (u64(idx[SLICE]) * max[SAMPLE] + idx[SAMPLE]) * max[SIDE] + idx[SIDE];

			if(layer > 0xFFFF)
				return INVALID_RESOURCE_INDEX;

			files[i].iid.layer = u16(layer);
		}

		return convert(out, files, flags);
	}

	//Find paths similar to the input path

	Helper::ErrorMessage Helper::convert(IGXI &out, const String &path, Flags flags) {

		List<String> files;
		
		if (ErrorMessage msg = findFiles(path, flags, files))
			return msg;

		return convert(out, files, flags);
	}

	//Convert to formats

	inline Buffer stbiWrite(const IGXI &in, const Vec3u16 &dim, ExternalFormat externFormat, u16 formatId, u16 layer, u16 z, u16 mip, f32 quality) {

		//TODO: Range checking

		if (!Helper::supportsExternalFormat(externFormat, in.format[formatId], quality)) {
			oic::System::log()->error("Unsupported format");
			return {};
		}

		GPUFormat format = in.format[formatId];
		usz stride = FormatHelper::getSizeBytes(format);

		const u8 *begin = in.data[formatId][mip].data() + (usz(layer) * dim.z + z) * dim.y * dim.z * stride;

		int len{};
		u8 *mem{};

		switch (externFormat) {

			case ExternalFormat::PNG:
				stbi_flip_vertically_on_write(true);
				mem = stbi_write_png_to_mem(begin, 0, dim.x, dim.y, int(stride), &len);
				break;

			default:
				oic::System::log()->error("Unsupported STBI format");
		}

		if (!mem)
			return {};

		Buffer buf = Buffer(mem, mem + len);
		stbi_image_free(mem);
		return buf;
	}

	//Wrapper

	bool Helper::supportsExternalFormat(ExternalFormat exFormat, GPUFormat format, f32 quality) {

		//Validate inputs

		if (quality <= 0 || quality > 1)
			return false;

		//Checking for custom formats

		if (format >= GPUFormat::sRGB8)
			return false;

		//Checking bit depth

		if(!HasFlags(exFormat, ExternalFormat::PROPERTY_SUPPORTS_1C << FormatHelper::getChannelCount(format)))
		   return false;

		if(!HasFlags(exFormat, ExternalFormat::PROPERTY_SUPPORTS_8B << FormatHelper::getStrideBytes(format)))
		   return false;

		//Checking quality

		if (quality == 1 && !HasFlags(exFormat, ExternalFormat::PROPERTY_CAN_BE_LOSSLESS))
			return false;

		if (quality != 1 && !HasFlags(exFormat, ExternalFormat::PROPERTY_CAN_BE_LOSSY))
			return false;

		//Checking format type

		if (format.value & 0x40)
			return HasFlags(exFormat, ExternalFormat::PROPERTY_SUPPORTS_FLOAT);

		if (format.value & 0x30)
			return HasFlags(exFormat, ExternalFormat::PROPERTY_SUPPORTS_SINT);

		if (format.value & 0x20)
			return HasFlags(exFormat, ExternalFormat::PROPERTY_SUPPORTS_UINT);

		if (format.value & 0x10)
			return HasFlags(exFormat, ExternalFormat::PROPERTY_SUPPORTS_SNORM);

		return HasFlags(exFormat, ExternalFormat::PROPERTY_SUPPORTS_UNORM);
	}

	Buffer Helper::toExternalFormat(
		const IGXI &in, ExternalFormat exFormat, GPUFormat format, const Vec3u16 &dim, u16 z, u16 layerId, u8 mip, f32 quality
	) {

		auto it = std::find(in.format.begin(), in.format.end(), format);

		if (it == in.format.end()) {
			oic::System::log()->error("GPUFormat not available in IGXI file");
			return {};
		}

		return stbiWrite(in, dim, exFormat, u16(it - in.format.begin()), layerId, z, mip, quality);
	}

	HashMap<ignis::GPUFormat, List<Pair<Helper::FileDesc, Buffer>>> Helper::toMemoryExternalFormat(const IGXI &igxi, f32 quality) {

		//TODO: Validate IGXI

		//Output to buffers

		HashMap<GPUFormat, List<Pair<Helper::FileDesc, Buffer>>> buffers;

		u16 layers = igxi.header.layers, mips = igxi.header.mips, formats = igxi.header.formats;

		for (u16 formatId = 0; formatId != formats; ++formatId) {

			Vec3u16 dim{ igxi.header.width, igxi.header.height, igxi.header.length };

			GPUFormat format = igxi.format[formatId];

			usz i{};

			for (const ExternalFormat &exFormat : allFormatsByPriority)
				if (supportsExternalFormat(exFormat, format))
					break;
				else ++i;

			if (i == _countof(allFormatsByPriority))
				continue;

			for(u8 mip = 0; mip != mips; ++mip) {

				for(u16 layer = 0; layer < layers; ++layer)
					for(u16 z = 0; z < dim.z; ++z) {

						//_z_layer_mip_formatName.extension

						static const List<String> allFormatExtensions {
							".png"
						};

						String suffix =
							(dim.z > 1 ? std::to_string(z) + "_" : "") +
							(layers > 1 ? std::to_string(layer) + "_" : "") +
							(mips > 1 ? std::to_string(mip) + "_" : "") +
							(formats > 1 ? GPUFormat::nameByValue(format.value) : "") +
							allFormatExtensions[i];

						buffers[format].push_back({
							{ 
								suffix, 
								ImageIdentifier{ z, layer, mip } 
							},
							toExternalFormat(igxi, allFormatsByPriority[i], format, dim, z, layer, mip, quality)
						});
					}

				dim = (dim.cast<Vec3f32>() / 2.f).ceil().cast<Vec3u16>();
			}
		}

		return buffers;
	}

	List<GPUFormat> Helper::toDiskExternalFormat(const IGXI &igxi, const String &path, f32 quality) {

		auto res = toMemoryExternalFormat(igxi, quality);

		if (res.size() != igxi.header.formats) {

			List<GPUFormat> unsupported = igxi.format;

			for (auto &elem : res) {

				auto it = std::find(unsupported.begin(), unsupported.end(), elem.first);

				if (it != unsupported.end())
					unsupported.erase(it);
			}

			return unsupported;
		}

		for (auto &elem : res)
			for (auto &img : elem.second)
				oic::System::files()->writeNew(path + img.first.path, img.second);

		return {};
	}

}