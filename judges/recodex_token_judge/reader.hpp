#ifndef RECODEX_TOKEN_JUDGE_READER_HPP
#define RECODEX_TOKEN_JUDGE_READER_HPP


#include <system/mmap_file.hpp>
#include <string>
#include <vector>
#include <limits>
#include <cstdint>
#include <cctype>


/**
 * Reader is a wrapper that mmaps file for reading and provide parsing function.
 * \tparam CHAR Base character type used for parsing (char by default).
 * \tparam OFFSET Base data type for numeric offsets.
 *         The offset determines maximal file size that can be processed.
 */
template <typename CHAR = char, typename OFFSET = std::uint32_t> class Reader
{
public:
	typedef CHAR char_t;
	typedef OFFSET offset_t;


	/**
	 * Internal structure that hold references to tokens.s
	 */
	class TokenRef
	{
	private:
		offset_t mOffset; ///< Relative position in the data file.
		offset_t mLength; ///< Length of the token.
		offset_t mLineNumber; ///< Line number where the token is present.
		offset_t mCharNumber; ///< Index of the first token character on its respective line.

	public:
		TokenRef(offset_t offset, offset_t length, offset_t lineNumber, offset_t charNumber)
			: mOffset(offset), mLength(length), mLineNumber(lineNumber), mCharNumber(charNumber)
		{
		}

		offset_t offset() const
		{
			return mOffset;
		}

		offset_t length() const
		{
			return mLength;
		}

		offset_t lineNumber() const
		{
			return mLine;
		}

		offset_t charNumber() const
		{
			return mChar;
		}
	};


	/**
	 * Wrapper representing one parsed line of tokens.
	 * Provide various accessors to token data.
	 */
	class Line
	{
		friend Reader<CHAR, OFFSET>;

	private:
		Reader &mReader;
		offset_t mLineNumber;
		std::vector<TokenRef> mTokens;

		// Line must be constructed by the Reader ...
		Line(Reader &reader, offset_t lineNumber) : mReader(reader), mLineNumber(lineNumber)
		{
		}

	public:
		/**
		 * Get the number of the line in the original file.
		 */
		offset_t lineNumber() const
		{
			return mLineNumber;
		}

		/**
		 * Returns the number of tokens on the line.
		 */
		std::size_t size() const
		{
			return mTokens.size();
		}


		/**
		 * Raw accessor to underlying TokenRef objects.
		 */
		const TokenRef &operator[](std::size_t idx) const
		{
			return mTokens[idx];
		}


		/**
		 * Get raw token string as plain old const char pointer (not necesarly null-terminated).
		 */
		const char_t *getCString(std::size_t idx) const
		{
			return mReader.getToken(mTokens[idx]);
		}


		/**
		 * Return the token as a copy wrapped in std string.
		 */
		std::string getString(std::size_t idx) const
		{
			return mReader.getToken(mTokens[idx]);
		}
	};


private:
	bpp::MMapFile mFile; ///< Underlying mmaped file.
	bool mIgnoreEmptyLines; ///< Empty lines are skipped completely.
	bool mAllowComments; ///< Allow comments (lines starting with '#'), which are completely skipped.
	bool mIgnoreLineEnds; ///< Treat end lines as regular whitespace.

	char_t *mData; ///< Mmaped data of the file.
	offset_t mOffset; ///< Offset from the beginning of the file (currently processed).
	offset_t mLength; ///< Total length of the file.
	offset_t mLineNumber; ///< Number of current line.
	offset_t mLineOffset; ///< Offset of the beginning of current line.


	/**
	 * Whether the end of line has been reached.
	 */
	bool eol()
	{
		return !eof() && mData[mOffset] == (char) '\n';
	}


	/**
	 * Skip any whitespace characters except for newline.
	 */
	void skipWhitespace()
	{
		while (!eof() && !eol() && std::isspace((char) mData[mOffset])) ++mOffset;
	}


	/**
	 * Skip currently processed token (any non-whitespace characters).
	 */
	void skipToken()
	{
		while (!eof() && !std::isspace((char) mData[mOffset])) ++mOffset;
	}


	/**
	 * Skip all characters until the end of line (including the newline character).
	 */
	void skipRestOfLine()
	{
		while (!eof() && !eol()) ++mOffset;
		if (!eof()) ++mOffset; // skip newline char
		++mLineNumber;
		mLineOffset = mOffset;
	}


	/**
	 * Have we reached a comment start (if permitted)?
	 */
	bool isCommentStart()
	{
		return mAllowComments && !eof() && mData[mOffset] == (char_t) '#';
	}


	/**
	 * Have we reached a start of a token?
	 */
	bool isTokenStart()
	{
		return !eof() && !std::isspace((char) mData[mOffset]) && (!mAllowComments || mData[mOffset] != (char_t) '#');
	}


	/**
	 * Retrieve const char reference to a token.
	 */
	const char_t *getToken(const TokenRef &token)
	{
		return mData + token.offset();
	}

public:
	Reader(bool ignoreEmptyLines, bool allowComments, bool ignoreLineEnds)
		: mIgnoreEmptyLines(ignoreEmptyLines), mAllowComments(allowComments), mIgnoreLineEnds(ignoreLineEnds),
		  mData(nullptr), mOffset(0), mLength(0)
	{
	}


	/**
	 * Open memory mapped file and initilize reader.
	 */
	void open(const std::string &fileName)
	{
		mFile.open(fileName);
		if (mFile.length() > std::numeric_limits<offset_t>::max() &&
			mFile.length() > std::numeric_limits<offset_t>::max() * sizeof(char_t)) {
			throw(bpp::RuntimeError() << "File " << fileName
									  << " is too large to be loaded by current configuration of Reader.");
		}
		if (mFile.length() % sizeof(char_t) != 0) {
			throw(bpp::RuntimeError() << "File " << fileName << " size is not divisible by selected char size.");
		}

		mData = (char_t *) mFile.getData();
		mOffset = 0;
		mLength = mFile.length() / sizeof(char_t);
		mLineNumber = 1;
		mLineOffset = 0;
	}


	/**
	 * Is the reader open for reading?
	 */
	bool opened() const
	{
		return mFile.opened();
	}


	/**
	 * Close the reader and underlying mmapped file.
	 */
	void close()
	{
		mFile.close();
		mData = nullptr;
		mOffset = mLength = 0;
	}


	/**
	 * Whether the end of file has been reached.
	 */
	bool eof()
	{
		return mOffset >= mLength;
	}


	/**
	 * Parse one line of tokens. If new lines are ignored, entire file is parsed.
	 * \return Unique pointer to a Line object.
	 */
	std::unique_ptr<Line> readLine()
	{
		if (eof()) {
			return std::unique_ptr<Line>();
		}

		auto line = bpp::make_unique<Line>(*this, mLineNumber);
		while (!eof()) {
			skipWhitespace();

			if (isTokenStart()) {
				// A regular token was encountered -- add it to the list.
				offset_t start = mOffset;
				skipToken();
				line.mTokens.push_back(TokenRef(start, mOffset - start, mLineNumber, mOffset - mLineOffset + 1));
			} else if (isCommentStart()) {
				// Comment was encountered, rest of the line is ignored.
				skipRestOfLine();

				if (mIgnoreLineEnds) continue; // new lines are ignored, lets continue loading tokens
				if (!line.mTokens.empty() || !mIgnoreEmptyLines) break; // line is non-empty or we return empty lines
			}
			if (eol()) {
				// End of line was encountered.
				skipRestOfLine();
				if (!mIgnoreLineEnds) break;
			}
		}

		return line;
	}
};


#endif
