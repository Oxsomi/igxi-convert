#include "igxi-tool.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize.h"

namespace igxi {

	//Load a given file and mips

	inline Helper::ErrorMessage load(
		List<Buffer> &out, u16 baseMip, u16 mips, const String &path,
		Helper::Flags flags, u16 &width, u16 &height, GPUFormat &format,
		const List<Array<u16, 5>> &sizes
	) {

		//Get file

		Buffer file;

		{
			IGXI::FileLoader loader(path);
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
			u8 *data = (u8*) stbi__load_main(&s, &x, &y, &comp, channelCount, &ri, 16);	

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

		if (x >= sizes[1] || y >= sizes[2] || x <= 0 || y <= 0) {
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

		GPUFormat format = GPUFormat::NONE;

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
				format = GPUFormat((channelCount - 1) | ((bytes - 1) << 2) | (u8(primitive) << 4));

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

	}

	//Find all files that correspond with the given path and parse their file description

	inline Helper::ErrorMessage findFiles(
		const String &path, Helper::Flags flags, List<Helper::FileDesc> &files, u16 &length, u16 &layers, u16 &mips
	) {

		//TODO: Load
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
	}

	//Copy memory from temporary image into our target

	inline Helper::ErrorMessage insertInto(
		Buffer &out, const Buffer &buf, u16 z, u16 layer, const IGXI::Header &header, const Array<u16, 5> &size
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

	Helper::ErrorMessage Helper::convert(IGXI &out, const String &path, Flags flags) {

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

		//Find appropriate files

		u16 length, layers, mips;

		List<FileDesc> files;
		
		if (ErrorMessage msg = findFiles(path, flags, files, length, layers, mips))
			return msg;

		//Get usage

		GPUMemoryUsage usage{};

		if (flags & MEMORY_SHARED)
			usage = GPUMemoryUsage::SHARED;

		if (flags & MEMORY_PREFER)
			usage = GPUMemoryUsage(u8(usage) | u8(GPUMemoryUsage::PREFER));

		if (flags & MEMORY_CPU_WRITE)
			usage = GPUMemoryUsage(u8(usage) | u8(GPUMemoryUsage::CPU_WRITE));

		if (flags & MEMORY_GPU_WRITE)
			usage = GPUMemoryUsage(u8(usage) | u8(GPUMemoryUsage::GPU_WRITE));

		//Output data

		out.header.flags = IGXI::Flags::CONTAINS_DATA;
		out.header.formats = 1;
		out.header.usage = usage;
		out.header.type = type;
		out.header.length = length;
		out.header.layers = layers;
		out.header.mips = mips;

		//Process files

		bool isFirst = true;

		List<Array<u16, 5>> sizes;

		for (FileDesc &file : files) {

			List<Buffer> fileData;

			u16 x, y;
			GPUFormat format;

			if (Helper::ErrorMessage msg = load(fileData, file.mip, flags & GENERATE_MIPS ? mips : 1, file.path, flags, x, y, format, sizes))
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

				for (Buffer &b : out.data[0]) {

					b.resize(layers * length * y * x * stride);

					sizes[mip] = { stride, x, y, length, layers };

					length = u16(std::ceil(f64(length) / 2));
					y = u16(std::ceil(f64(y) / 2));
					x = u16(std::ceil(f64(x) / 2));
					++mip;
				}

			} else if (x != out.header.width || y != out.header.height)
				return Helper::CONFLICTING_IMAGE_SIZE;

			else if(format != out.format[0])
				return Helper::CONFLICTING_IMAGE_FORMAT;

			u16 mip{};

			for (const Buffer &buf : fileData)
				if (Helper::ErrorMessage msg = insertInto(out, buf, 0, file.z, file.layer, file.mip + mip, sizes[mip]))
					return msg;
				else 
					++mip;
		}

		//Compress

		if (flags & Helper::DO_COMPRESSION) {
			//TODO: Set format to compressed formats
			//TODO: Convert everytime a FULL layer is added to minimize memory usage
		}

		return SUCCESS;
	}

}