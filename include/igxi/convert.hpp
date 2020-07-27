#pragma once
#include "igxi/igxi.hpp"
#include "types/vec.hpp"

namespace igxi {

	//Supports the following formats:
	//	hdr (defaulted as 16-bit float)
	//	png/jpg/bmp/gif/pic/pnm/tga (defaulted as 8-bit unorm)
	//
	enum class ExternalFormat : u32 {

		//Properties

		PROPERTY_SUPPORTS_FLOAT			= 0x00001,
		PROPERTY_SUPPORTS_UNORM			= 0x00002,
		PROPERTY_SUPPORTS_SNORM			= 0x00004,
		PROPERTY_SUPPORTS_UINT			= 0x00008,
		PROPERTY_SUPPORTS_SINT			= 0x00010,

		PROPERTY_SUPPORTS_8B			= 0x00020,
		PROPERTY_SUPPORTS_16B			= 0x00040,
		PROPERTY_SUPPORTS_32B			= 0x00080,
		PROPERTY_SUPPORTS_64B			= 0x00100,

		PROPERTY_SUPPORTS_1C			= 0x00200,
		PROPERTY_SUPPORTS_2C			= 0x00400,
		PROPERTY_SUPPORTS_3C			= 0x00800,
		PROPERTY_SUPPORTS_4C			= 0x01000,

		PROPERTY_CAN_BE_LOSSLESS		= 0x02000,
		PROPERTY_CAN_BE_LOSSY			= 0x04000,

		PROPERTY_SUPPORTS_BC			= 0x08000,
		PROPERTY_SUPPORTS_ASTC			= 0x10000,

		//File formats

		PNG = 
			PROPERTY_CAN_BE_LOSSLESS |
			PROPERTY_SUPPORTS_UNORM | 
			PROPERTY_SUPPORTS_8B | PROPERTY_SUPPORTS_16B | 
			PROPERTY_SUPPORTS_1C | PROPERTY_SUPPORTS_2C | PROPERTY_SUPPORTS_3C | PROPERTY_SUPPORTS_4C, 

		/* TODO:

		HDR	= 
			PROPERTY_SUPPORTS_FLOAT | 
			PROPERTY_SUPPORTS_16B | PROPERTY_SUPPORTS_32B | 
			PROPERTY_SUPPORTS_3C, 

		JPG	=
			PROPERTY_SUPPORTS_UNORM |
			PROPERTY_SUPPORTS_8B |
			PROPERTY_SUPPORTS_3C,

		TGA	= 
			PROPERTY_SUPPORTS_UNORM |
			PROPERTY_SUPPORTS_8B |
			PROPERTY_SUPPORTS_1C | PROPERTY_SUPPORTS_2C | PROPERTY_SUPPORTS_3C | PROPERTY_SUPPORTS_4C,

		BMP, 
		GIF, 
		PIC, 
		PNM, 
		//TIFF,		TODO
		//DDS,		TODO (almost any format)
		//OPENEXR,	TODO (almost any format)
		PSD
		*/
	};

	enumFlagOverloads(ExternalFormat);

	//A helper for converting to IGXI format
	//Conversion from IGXI isn't always lossless,
	//	since some output formats can't represent the input format
	//
	struct Helper {

		//A list of all supported formats and their extensions

		static constexpr ExternalFormat allFormatsByPriority[] = {
			ExternalFormat::PNG
		};

		//A location of the image
		struct ImageIdentifier {
			u16 z, layer;
			u8 mip;
		};

		//Flags for a full IGXI file
		//
		//Types:
		//	Only one of PROPERTY_TYPE can be set; if none are set, 2D is assumed
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
		//Load hints:
		//
		//	If GENERATE_MIPS is set; it will generate linear mips
		//		Otherwise it will look for a number (which can have a separator in-between)
		//		path.0, path0, path-0, etc.
		//
		//	If DO_COMPRESSION is set; it will attempt to find suitable compression and ONLY use that
		//		Both S3TC/BC and ASTC
		//
		//Format hints:
		//
		//	If no format flags are specified, it will auto detect based on the read format (specified in Helper comment)
		//
		//	If IS_SRGB is set; base format can be either RGBA8 or RGB8
		//
		//	Only one of the following can be set (PROPERTY_CHANNELS):
		//		IS_R	(1 channel)
		//		IS_RG	(2 channels)
		//		IS_RGB	(3 channels)
		//		IS_RGBA (4 channels)
		//		INPUT_CHANNEL_COUNT (use the input channel count; default, no flag)
		//
		//	Only one of the following can be set (PROPERTY_PRIMTIIVE):
		//		IS_SINT		(signed integer type)
		//		IS_UINT		(unsigned integer type)
		//		IS_UNORM	(unsigned normalized float)
		//		IS_SNORM	(signed normalized float)
		//		IS_FLOAT	(regular float)
		//		INPUT_PRIMITIVE (use the input primitive type; default, no flag)
		//	
		//	Only one of the following can be set (PROPERTY_BITS)
		//		IS_8_BIT	(8 bits per channel; can't be a regular float)
		//		IS_16_BIT	(16 bits per channel; can't be a regular float)
		//		IS_32_BIT	(32 bits per channel; can't be a unorm or snorm)
		//		IS_64_BIT	(64 bits per channel; can't be a unorm or snorm)
		//		INPUT_BIT_COUNT (use the input bit count; default, no flag)
		//
		//Memory hints:
		//
		//	Location:
		//		MEMORY_LOCAL	(On dedicated GPU device; default, no flag)
		//		MEMORY_SHARED	(In shared memory)
		//		MEMORY_PREFER	(It prefers to be placed somewhere;
		//						 PREFER LOCAL is placed in shared memory if the dedicated memory is low,
		//						 PREFER SHARED is placed in local memory if shared memory is fast (e.g. on integraded GPUs))
		//
		//	Access:
		//		MEMORY_CPU_WRITE	(The resource can be written to from CPU)
		//		MEMORY_GPU_WRITE	(The resource can be written to from GPU)
		//
		//
		enum Flags : u32 {

			//Type

			IS_1D = 1 << 0,
			IS_2D = 1 << 1,
			IS_3D = 1 << 2,
			IS_CUBE = 1 << 3,
			IS_MS = 1 << 4,

			PROPERTY_TYPE = IS_1D | IS_2D | IS_3D | IS_CUBE | IS_MS,

			IS_ARRAY = 1 << 5,

			//Load hints

			GENERATE_MIPS = 1 << 6,
			DO_COMPRESSION = 1 << 7,

			//Colorspace hint

			IS_SRGB = 1 << 8,

			//Channels

			INPUT_CHANNEL_COUNT = 0,
			IS_R = 1 << 9,
			IS_RG = 1 << 10,
			IS_RGB = 1 << 11,
			IS_RGBA = 1 << 12,

			PROPERTY_CHANNELS = IS_R | IS_RG | IS_RGB | IS_RGBA,

			//Primitive

			INPUT_PRIMITIVE = 0,
			IS_SINT = 1 << 13,
			IS_UINT = 1 << 14,
			IS_UNORM = 1 << 15,
			IS_SNORM = 1 << 16,
			IS_FLOAT = 1 << 17,

			PROPERTY_PRIMTIIVE = IS_SINT | IS_UINT | IS_UNORM | IS_SNORM | IS_FLOAT,

			//Bits

			INPUT_BIT_COUNT = 0,
			IS_8_BIT = 1 << 18,
			IS_16_BIT = 1 << 19,
			IS_32_BIT = 1 << 20,
			IS_64_BIT = 1 << 21,

			PROPERTY_BITS = IS_8_BIT | IS_16_BIT | IS_32_BIT | IS_64_BIT,

			//Memory allocation

			MEMORY_LOCAL = 0,
			MEMORY_SHARED = 1 << 22,

			MEMORY_REQUIRE = 0,
			MEMORY_PREFER = 1 << 23,
			MEMORY_CPU_READ = 1 << 24,
			MEMORY_CPU_WRITE = 1 << 25,
			MEMORY_GPU_WRITE = 1 << 26,

			//Mip generation; TODO: needs a count

			MIP_LINEAR = 0,
			MIP_NEAREST = 1 << 27,
			MIP_MIN = 1 << 28,
			MIP_MAX = 1 << 29,

			//Default values

			NONE = 0,
			DEFAULT =
				GENERATE_MIPS | DO_COMPRESSION | IS_2D |
				INPUT_BIT_COUNT	| INPUT_CHANNEL_COUNT | INPUT_PRIMITIVE | MIP_LINEAR,

			DEFAULT_NO_COMPRESSION = DEFAULT & ~DO_COMPRESSION

		};

		//Errors if the conversion fails:
		//
		//INVALID_TYPE is generated if type flags are combined that shouldn't be:
		//	
		//	IS_2D with IS_1D, IS_3D, IS_CUBE,
		//	IS_1D with IS_2D, IS_3D, IS_CUBE, IS_MS
		//	IS_3D with IS_1D, IS_2D, IS_CUBE, IS_MS, IS_ARRAY
		//	IS_CUBE with IS_1D, IS_2D, IS_3D, IS_MS
		//	IS_MS with IS_1D, IS_3D, IS_CUBE
		//
		//INVALID_CHANNELS is generated if any of the channel flags are combined which each other
		//	e.g. IS_R, IS_RG, IS_RGB, IS_RGBA
		//
		//INVALID_PRIMITIVE is generated if any of the primitive bits are combined which each other
		//	e.g. IS_INT, IS_UINT, IS_UNORM, IS_SNORM, IS_FLOAT
		//
		//INVALID_BITS is generated if any of the channel size bits are combined which each other
		//	e.g. IS_8_BIT, IS_16_BIT, IS_32_BIT, IS_64_BIT
		//
		//INVALID_FORMAT is generated if an invalid combination is used between bits, primitive and channels
		//	8-bit images with float primitive
		//	32-bit/64-bit images with snorm/unorm primitive
		//	IS_SRGB with formats other than RGB8/RGBA8 (unorm)
		//	
		//INVALID_FILE_PATH is generated if the file path provided couldn't be opened
		//INVALID_FILE_DATA is generated if the file couldn't be understood
		//INVALID_FILE_BOUNDS is generated if the file is empty
		//INVALID_IMAGE_SIZE is generated if the parsed size is too small or too big
		//INVALID_RESOURCE_INDEX is if the mip, layer or z is out of bounds
		//INVALID_OPERATION is generated if an operation is unimplemented
		//
		//MISSING_FACE is if a face of the cube is missing
		//MISSING_MIP is if GENERATE_MIP is off and one of the mips isn't provided
		//MISSING_PATHS is if the paths are missing
		//MISSING_RESOURCE_INDEX is if one of the subresources is missing from input
		//
		//CONFLICTING_IMAGE_SIZE is if one of the input images is a different size than another
		//
		//CONFLICTING_IMAGE_FORMAT is if one of the input images has a different format than another
		//	Happens when non-hdr textures are combined with hdr
		//		or when psd 16-bit is combined with 8-bit or hdr
		//		or if the channel count isn't the same
		//	Solution: Set the output format manually or convert to the same format
		//
		//CONFLICTING_RESOURCE_INDEX is when a subresource is referenced multiple times
		//
		//TOO_MANY_MIPS is generated if GENERATE_MIPS is on but more mips than the base mip are passed
		//
		enum ErrorMessage : u8 {

			SUCCESS,

			INVALID_TYPE = 0x1,
			INVALID_CHANNELS,
			INVALID_PRIMITIVE,
			INVALID_BITS,
			INVALID_FORMAT,
			INVALID_FILE_PATH,
			INVALID_FILE_DATA,
			INVALID_FILE_BOUNDS,
			INVALID_IMAGE_SIZE,
			INVALID_RESOURCE_INDEX,
			INVALID_FILE_NAME_FACE,
			INVALID_FILE_NAME_SLICE,
			INVALID_FILE_NAME_MIP,
			INVALID_OPERATION,

			MISSING_FACE = 0x21,
			MISSING_PATHS,
			MISSING_RESOURCE_INDEX,

			CONFLICTING_IMAGE_SIZE = 0x41,
			CONFLICTING_IMAGE_FORMAT,
			CONFLICTING_RESOURCE_INDEX,

			TOO_MANY_MIPS = 0x61

		};

		//A description of a file
		//Including where in the resource it is located
		struct FileDesc {
			String path;
			ImageIdentifier iid;
		};

		//Convert a single file into an IGXI file
		static ErrorMessage convert(IGXI &out, const String &path, Flags flags = DEFAULT);

		//Convert a couple files (with  into an IGXI file
		static ErrorMessage convert(IGXI &out, const List<String> &paths, Flags flags = DEFAULT);

		//Convert a couple files (with description) into an IGXI file
		static ErrorMessage convert(IGXI &out, const List<FileDesc> &descs, Flags flags = DEFAULT);

		//Convert to an IGXI description
		//static ErrorMessage convert(const IGXI &out, const Description &desc, Flags flags = DEFAULT);

		//Convert an IGXI's gpu format index to an external format format
		//Returns an empty buffer if it cannot be converted
		//"Quality" can be set to 0->1 depending on how much detail should be kept
		static Buffer toExternalFormat(const IGXI &in, ExternalFormat exFormat, ignis::GPUFormat format, const Vec3u16 &dim, u16 z, u16 layerId, u8 mipId, f32 quality = 1);

		//Whether or not the mentioned format can be represented by PNG format
		//If quality == 1, the format has to be capable of representing lossless images
		static bool supportsExternalFormat(ExternalFormat exFormat, ignis::GPUFormat format, f32 quality = 1);

		//Output IGXI as png/jpg/hdr/dds/etc. depending on the GPUFormat
		//Returns only the formats that are supported by the external file formats, so check result.size() with in.headers.formats
		static HashMap<ignis::GPUFormat, List<Pair<FileDesc, Buffer>>> toMemoryExternalFormat(const IGXI &in, f32 quality = 1);

		//Output IGXI as png/jpg/hdr/dds/etc. depending on the GPUFormat
		//Returns the unsupported formats
		//On success (and successful write to "path"), the resulting List will be empty
		//As this can return any type of image format, the path should be without an extension
		//It also outputs layers as follows: path_z_layer_mip_formatName if multiple layers, mips or formats are present
		static List<ignis::GPUFormat> toDiskExternalFormat(const IGXI &in, const String &path, f32 quality = 1);

	};

}