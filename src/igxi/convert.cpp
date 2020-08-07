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

	//Raw channel access

	inline u64 readValue(usz stride, const u8 *data) {
		switch (stride) {
			case 1:		return *data;
			case 2:		return *(const u16*)data;
			case 4:		return *(const u32*)data;
			case 8:		return *(const u64*)data;
			default:	return 0;
		}
	}

	inline void writeValue(usz stride, u8 *data, u64 val) {
		switch (stride) {
			case 1:		*data = u8(val);			break;
			case 2:		*(u16*)data = u16(val);		break;
			case 4:		*(u32*)data = u32(val);		break;
			case 8:		*(u64*)data = u64(val);		break;
			default:	0;
		}
	}

	//Spaghetti monster of conversion between types

	template<typename T>
	inline u64 toU64(const T &t) {
		u64 v{};
		*(T*)&v = t;
		return v;
	}

	inline bool canConvert(GPUFormat target, GPUFormat input) {
		return 
			FormatHelper::getType(target) == FormatHelper::getType(input) && 
			FormatHelper::getType(target) == GPUFormatType::FLOAT;
	}

	inline u64 convert(GPUFormat target, GPUFormat input, const u64 &val) {

		switch (FormatHelper::getStrideBytes(target)) {

			case 2:	{

				f16 v;

				if(FormatHelper::getStrideBytes(input) == 4)
					v = f16(*(const f32*)&val);
				else
					v = f16(*(const f64*)&val);

				if (v.lacksPrecision())
					v = f16::max();

				return toU64(v);
			}

			case 4: {

				if(FormatHelper::getStrideBytes(input) == 2)
					return toU64(f32(*(const f16*)&val));

				f32 v = f32(*(const f64*)&val);

				if ((*(flp32*)&v).lacksPrecision())
					v = f32_MAX;

				return toU64(v);
			}

			case 8:

				if(FormatHelper::getStrideBytes(input) == 2)
					return toU64(f64(*(const f16*)&val));

				return toU64(f64(*(const f32*)&val));

			default: return 0;
		}

	}

	//Load a given file and mips

	inline Helper::ErrorMessage load(
		List<Buffer> &out, const Buffer &buf, u16 baseMip, u16 mips,
		Helper::Flags flags, u16 &width, u16 &height, GPUFormat &format,
		List<Array<u16, 5>> &sizes
	) {

		//Read image via stbi
		//Supports jpg/png/bmp/gif/psd/pic/pnm/hdr/tga
		//Preserve all bit depth

		stbi__result_info ri;

		stbi__context s;
		stbi__start_mem(&s, buf.data(), int(buf.size()));

		u8 *data{};

		int x{}, y{}, comp{}, stride = 1;

		const bool is1D = flags & Helper::IS_1D;

		GPUFormat currentFormat = GPUFormat::NONE;

		int channelCount;

		switch (flags & Helper::PROPERTY_CHANNELS) {

			case 0: channelCount = 0; break;

			case Helper::IS_R: channelCount = 1; break;
			case Helper::IS_RG: channelCount = 2; break;
			case Helper::IS_RGB: channelCount = 3; break;
			case Helper::IS_RGBA: channelCount = 4; break;

			default: return Helper::INVALID_CHANNELS;

		}

		bool inputFloat{}, input16Bit{};

		if (stbi__hdr_test(&s)) {

			data = (u8*) stbi__hdr_load(&s, &x, &y, &comp, channelCount, &ri);
			stride = 4;
			currentFormat = GPUFormat(u16((comp - 1) | (2 << 2) | (u8(GPUFormatType::FLOAT) << 4)));;
			inputFloat = true;

		} else {

			data = (u8*) stbi__load_main(&s, &x, &y, &comp, channelCount, &ri, 16);	
			stride += int(input16Bit = ri.bits_per_channel == 16);
			currentFormat = GPUFormat(u16((comp - 1) | ((stride - 1) << 2) | (u8(GPUFormatType::UNORM) << 4)));;
		}

		if (!channelCount)
			channelCount = comp;

		if (!data)
			return Helper::INVALID_FILE_DATA;

		if (!channelCount) {
			stbi_image_free(data);
			return Helper::INVALID_FILE_DATA;
		}

		//Convert to correct dimension

		if (is1D) {
			x *= y;
			y = 1;
		}

		//Technically, images could be u16_MAX as well, but I want that reserved as an error code
		//
		if(x >= u16_MAX || y >= u16_MAX || x <= 0 || y <= 0) {
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
			
			default:
				stbi_image_free(data);
				return Helper::INVALID_PRIMITIVE;

		}

		//Get bits

		u32 bytes;

		switch (flags & Helper::PROPERTY_BITS) {

			case 0: bytes = inputFloat || input16Bit ? 2 : 1; break;

			case Helper::IS_8_BIT: bytes = 1; break;
			case Helper::IS_16_BIT: bytes = 2; break;
			case Helper::IS_32_BIT: bytes = 4; break;
			case Helper::IS_64_BIT: bytes = 8; break;

			default: 
				stbi_image_free(data);
				return Helper::INVALID_BITS;

		}

		//Get format

		format = GPUFormat::NONE;

		if ((bytes == 1 || bytes == 2) && channelCount == 3)
			channelCount = 4;

		if (flags & Helper::IS_SRGB) {

			if (bytes == 1 && channelCount == 4)
				format = GPUFormat::srgba8;

		} else {

			if(
				!(bytes == 1 && primitive == GPUFormatType::FLOAT) && 
				!(bytes > 2 && !(u8(primitive) & u8(GPUFormatType::PROPERTY_IS_UNNORMALIZED)))
			)
				format = GPUFormat(u16((channelCount - 1) | ((bytes - 1) << 2) | (u8(primitive) << 4)));

		}

		if (GPUFormat::idByValue(format.value) >= GPUFormat::idByValue(GPUFormat::NONE)) {
			stbi_image_free(data);
			return Helper::INVALID_FORMAT;
		}

		//Get strides

		if (sizes.empty()) {

			u16 xx = u16(x), yy = u16(y);

			//TODO: Assumption not always correct

			width = xx;
			height = yy;

			sizes.resize(mips);
			out.resize(mips);

			u16 j = (flags & Helper::GENERATE_MIPS) ? mips : 1;

			for (u16 i = 0; i < j; ++i) {
				sizes[baseMip + i] = { u16(FormatHelper::getSizeBytes(format)), xx, yy, 1, 1 };
				xx = u16(std::ceil((f32(xx) / 2.f)));
				yy = u16(std::ceil((f32(yy) / 2.f)));
			}
		}
		else {
			stbi_image_free(data);
			return Helper::INVALID_OPERATION;
		}

		if (baseMip != 0) {
			stbi_image_free(data);
			return Helper::INVALID_OPERATION;
		}
			
		//TODO:
			/*{

			update layers/mip

			if (x != sizes[baseMip][1] || y != sizes[baseMip][2]) {
				stbi_image_free(data);
				return Helper::INVALID_IMAGE_SIZE;
			}
		}*/

		if (format != currentFormat) {

			if (!canConvert(format, currentFormat))
				return Helper::INCOMPATIBLE_FORMATS;

			Buffer &converted = out[baseMip] = Buffer(usz(bytes) * channelCount * x * y);

			u8 *convertedPtr = (u8*)converted.data();

			usz copyStride = std::min(channelCount, comp);
			usz copyLength = copyStride * x * y;

			for (usz i = 0; i < copyLength; ++i) {

				usz channel = i % copyStride;
				usz xy = i / copyStride;

				u64 val = convert(
					format, 
					currentFormat,
					readValue(stride, data + usz(stride) * (channel + xy * comp))
				);

				writeValue(bytes, convertedPtr + usz(bytes) * (channel + xy * channelCount), val);
			}

		}
		else out[baseMip] = Buffer(data, data + usz(stride) * comp * x * y);

		//Generate mips

		if (flags & Helper::GENERATE_MIPS) {
			stbi_image_free(data);
			return Helper::INVALID_OPERATION;
		}

		//Use optimal format for mip generation; might be sRGB, float format, etc.

		//TODO: Only supported with a few formats
		//TODO: Use premultiplied alpha before generating mips
		//		Get it back somehow?

		//TODO: MIP_NEAREST, MIP_LINEAR, MIP_MIN, MIP_LINEAR
		//TODO: stbir filters
		//TODO: Report progress so you can see how much has been converted

		//stbir_resize_float(...);
		//Copy into out[baseMip + n] where n < mips

		stbi_image_free(data);
		return Helper::SUCCESS;
	}

	inline Helper::ErrorMessage load(
		List<Buffer> &out, u16 baseMip, u16 mips, const String &path,
		Helper::Flags flags, u16 &width, u16 &height, GPUFormat &format,
		List<Array<u16, 5>> &sizes
	) {

		//Get file

		Buffer file;

		{
			IGXI::File loader(path, false);

			usz start{};

			if (loader.readRegion(file.data(), start, loader.size()))
				return Helper::INVALID_FILE_PATH;
		}

		if (file.size() >= (usz(1) << (sizeof(int) * 8)))
			return Helper::INVALID_FILE_BOUNDS;

		if (Helper::ErrorMessage errorMessage = load(out, file, baseMip, mips, flags, width, height, format, sizes))
			return errorMessage;

		return Helper::ErrorMessage::SUCCESS;
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

		std::memcpy(out.data() + (usz(layer) * size[3] + z) * oneImg, buf.data(), oneImg);
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

		IGXI old = out;

		out = {};
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

			u16 x{}, y{};
			GPUFormat format = GPUFormat::NONE;

			if (file.path.empty()) {

				//Attempt to load one of multiple specified external formats
				//(like HDR or PNG can both be supplied, 
				//but if the flags doesn't support one of them it will pick the other)

				if (old.data.empty())
					return Helper::INVALID_FILE_DATA;

				Helper::ErrorMessage last = Helper::SUCCESS;

				for(auto &elem : old.data)
					if (
						(last = 
							load(
								fileData, elem[file.iid.layer], 
								file.iid.mip, flags & GENERATE_MIPS ? mips :  1, 
								flags, x, y, format, sizes
							)
						) == Helper::SUCCESS
					)
						break;

				if (last != Helper::SUCCESS)
					return last;

			}

			//Attemp to load from file

			else if (ErrorMessage msg = load(
				fileData, 
				file.iid.mip, flags & GENERATE_MIPS ? mips : 1, 
				file.path,
				flags, x, y, format, sizes
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

		if (!Helper::supportsExternal(externFormat, in.format[formatId], quality)) {
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

	bool Helper::supportsExternal(ExternalFormat exFormat, GPUFormat format, f32 quality) {

		//Validate inputs

		if (quality <= 0 || quality > 1)
			return false;

		//Checking for custom formats

		if (format > GPUFormat::srgba8)
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

	Buffer Helper::toExternal(
		const IGXI &in, ExternalFormat exFormat, GPUFormat format, const Vec3u16 &dim, u16 z, u16 layerId, u8 mip, f32 quality
	) {

		auto it = std::find(in.format.begin(), in.format.end(), format);

		if (it == in.format.end()) {
			oic::System::log()->error("GPUFormat not available in IGXI file");
			return {};
		}

		return stbiWrite(in, dim, exFormat, u16(it - in.format.begin()), layerId, z, mip, quality);
	}

	HashMap<ignis::GPUFormat, List<Pair<Helper::FileDesc, Buffer>>> Helper::toMemoryExternal(const IGXI &igxi, f32 quality) {

		//TODO: Validate IGXI

		//Output to buffers

		HashMap<GPUFormat, List<Pair<FileDesc, Buffer>>> buffers;

		u16 layers = igxi.header.layers, mips = igxi.header.mips, formats = igxi.header.formats;

		for (u16 formatId = 0; formatId != formats; ++formatId) {

			Vec3u16 dim{ igxi.header.width, igxi.header.height, igxi.header.length };

			GPUFormat format = igxi.format[formatId];

			usz i{};

			for (const ExternalFormat &exFormat : allFormatsByPriority)
				if (supportsExternal(exFormat, format))
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
							toExternal(igxi, allFormatsByPriority[i], format, dim, z, layer, mip, quality)
						});
					}

				dim = (dim.cast<Vec3f32>() / 2.f).ceil().cast<Vec3u16>();
			}
		}

		return buffers;
	}

	List<GPUFormat> Helper::toDiskExternal(const IGXI &igxi, const String &path, f32 quality) {

		auto res = toMemoryExternal(igxi, quality);

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

	Texture::Info Helper::convert(const IGXI &in, const Graphics &g, GPUFormat hint) {

		u16 formatId = in.header.formats;

		//Check if our hinted one exists and is supported

		if (hint != GPUFormat::NONE) {
			for (u16 i = 0; i < in.header.formats; ++i)
				if (in.format[i] == hint) {

					if (!g.supportsFormat(in.format[i]))
						oic::System::log()->fatal("Unsupported requested GPUFormat by device");

					formatId = i;
					break;
				}
		}

		else for(u16 i = 0; i < in.header.formats; ++i)
			if (g.supportsFormat(in.format[i])) {
				formatId = i;
				break;
			}

		if(formatId == in.header.formats)
			oic::System::log()->fatal("Unsupported GPUFormats in texture by device");

		GPUFormat format = in.format[formatId];

		Texture::Info inf = Texture::Info(
			in.header.type, 
			Vec3u16(in.header.width, in.header.height, in.header.length), 
			format, in.header.usage,
			in.header.mips, in.header.layers, 
			1, true
		);

		if (u8(in.header.flags) & u8(IGXI::Flags::CONTAINS_DATA))
			inf.init(in.data[formatId]);

		return inf;
	}

	oicExposedEnum(ErrorMessageExposed, u8,

		Success,

		Invalid_type = 0x1,
		Invalid_channels,
		Invalid_primitive,
		Invalid_bits,
		Invalid_format,
		Invalid_file_path,
		Invalid_file_data,
		Invalid_file_bounds,
		Invalid_image_size,
		Invalid_resource_index,
		Invalid_file_name_face,
		Invalid_file_name_slice,
		Invalid_file_name_mip,
		Invalid_operation,

		Missing_face = 0x21,
		Missing_paths,
		Missing_resource_index,

		Conflicting_image_size = 0x41,
		Conflicting_image_format,
		Conflicting_resource_index,

		Too_many_mips = 0x61
	);

	Texture::Info Helper::loadMemoryExternal(const Buffer &data, const Graphics &g, Flags flags) {

		IGXI out;
		out.data.push_back({ data });

		ErrorMessage errorMessage = convert(out, List<FileDesc>{ {} }, flags);

		if (errorMessage != SUCCESS)
			oic::System::log()->fatal(ErrorMessageExposed::nameByValue((ErrorMessageExposed::_E)errorMessage));

		return convert(out, g);
	}

	Texture::Info Helper::loadDiskExternal(const String &path, const Graphics &g, Flags flags) {
		return loadMemoryExternal(oic::System::files()->readToBuffer(path), g, flags);
	}

}