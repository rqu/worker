/*
 * Author: Martin Krulis <krulis@ksi.mff.cuni.cz>
 * Last Modification: 2.7.2013
 * License: CC 3.0 BY-NC (http://creativecommons.org/)
 */
#ifndef BPPLIB_SYSTEM_MMAP_FILE_HPP
#define BPPLIB_SYSTEM_MMAP_FILE_HPP

#include <misc/exception.hpp>

#include <iostream>


#ifdef _WIN32
	#define NOMINMAX
	#include <windows.h>

	// This macro is defined in wingdi.h, I do non want ERROR macro in my projects!
	#ifdef ERROR
	#undef ERROR
	#endif
#else
	#include <unistd.h>
	#include <sys/mman.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <fcntl.h>
#endif

namespace bpp {


/**
 * \brief MultiOS wrapper for read-only mmaped files.
 *
 * This class is used to map database files into memory,
 * so we can get faster access to them.
 */
class MMapFile
{
private:
#ifdef _WIN32
	typedef LONGLONG length_t;
#else
	typedef size_t length_t;
#endif

	void *mData;		///< Pointer to memory area where the file is mapped.
	length_t mLength;	///< Total size of the mapped file.
	std::string mFileName;

#ifdef _WIN32
	HANDLE mFile;		///< Windows file handle.
	HANDLE mMappedFile;	///< Windows mapped object handle.
#else
	int mFile;			///< Unix file handle.
#endif


public:
	MMapFile() : mData(NULL), mLength(0),
#ifdef _WIN32
		mFile(NULL), mMappedFile(NULL)
#else
		mFile(0)
#endif
	{}
	
	/**
	 * \brief When the object is destroyed, the file is unmapped and closed.
	 */
	~MMapFile()
	{
		close();
	}


	/**
	 * \brief Open and map file into memory.
	 * \param fileName Path to a file being opened.
	 * \throws RuntimeError if error occurs.
	 * \note If called multiple times, current file is closed before another is opened.
	 */
	void open(const std::string &fileName)
	{
		close();
		mFileName = fileName;

	#ifdef _WIN32
		// Create file handle.
		mFile = CreateFileA(mFileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
		if (mFile == INVALID_HANDLE_VALUE)
			throw RuntimeError("Cannot open selected file.");
		
		// Get the file size.
		LARGE_INTEGER tmpSize;
		if (!GetFileSizeEx(mFile, &tmpSize))
			throw RuntimeError("Cannot get file size.");
		mLength = tmpSize.QuadPart;

		// Create read only mapping object.
		mMappedFile = CreateFileMapping(mFile, NULL, PAGE_READONLY, 0, 0, NULL);
		if (mMappedFile == NULL)
			throw RuntimeError("Cannot create mapped file object.");

		// Map the entire file to virtual memory space.
		mData = MapViewOfFile(mMappedFile, FILE_MAP_READ, 0, 0, 0);
		if (mData == NULL)
			throw RuntimeError("Cannot map view of file.");
	#else
		// Create file handle.
		mFile = ::open(mFileName.c_str(), O_RDONLY);
		if (mFile == -1)
			throw RuntimeError("Cannot open selected file.");

		// Get the file size.
		struct stat fileStat;
		if (::fstat(mFile, &fileStat) == -1)
			throw RuntimeError("Cannot get file size.");
		mLength = fileStat.st_size;

		// Map the entire file to virtual memory space.
		mData = ::mmap(NULL, mLength, PROT_READ, MAP_PRIVATE, mFile, 0);
		if (mData == MAP_FAILED) {
			mData = NULL;
			throw RuntimeError("Cannot mmap the file.");
		}
	#endif
	};


	/**
	 * \brief Get a pointer to memory block where the file is mapped.
	 * \return Valid pointer if the file was mapped, NULL pointer otherwise.
	 */
	void* getData() const
	{
		return mData;
	}


	/**
	 * Return the length of the file.
	 */
	std::size_t length() const
	{
		return (std::size_t)mLength;
	}


	/**
	 * \brief Check whether the file has been opened.
	 * \return True if the file was opened (and mapped), false otherwise.
	 */
	bool opened() const
	{
		return getData() != NULL;
	}

	
	/**
	 * \brief Unmap the file and close it.
	 */
	void close()
	{
		if (!opened()) return;

	#ifdef _WIN32
		if (!UnmapViewOfFile(mData))
			throw RuntimeError("Cannot unmap view of file.");
		mData = NULL;

		if (!CloseHandle(mMappedFile) || !CloseHandle(mFile))
			throw RuntimeError("Cannot close mapped file.");
		mMappedFile = mFile = NULL;
	#else
		if (::munmap(mData, mLength) == -1)
			throw RuntimeError("Cannot unmap file.");
		mData = NULL;

		if (::close(mFile) == -1)
			throw RuntimeError("Cannot close mapped file.");
		mFile = 0;
	#endif
	}


	/**
	 * \brief Try to prepopulate virt. memory pages by the file data.
	 *
	 * This function is useful only if the file fits the memory.
	 * It expects only 4kB pages.
	 */
	void populate()
	{
		if (!opened())
			throw RuntimeError("The file must be opened before prepopulation.");

		// Traverse the mapped file accessing first dword on each page.
		unsigned x, *data = (unsigned*)getData();
		for (length_t i = 0; i < mLength / 4096; ++i) {
			x ^= *data;							// read from page
			data += 4096 / sizeof(unsigned);	// move to another page
		}
	}
};

}
#endif
