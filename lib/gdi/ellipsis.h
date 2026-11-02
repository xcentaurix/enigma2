#ifndef __lib_gdi_ellipsis_h
#define __lib_gdi_ellipsis_h

/*
	The rules of the ellipsis: a text that doesn't fit its area is cut and ends with an ellipsis ("…").

	eTextPara (font.cpp) lays the text out and measures; this file decides where a cut is allowed. It needs nothing but
	the standard library, so that it can be tested on the host (tests/ellipsis_test.cpp: g++ -std=c++17 -I. ...).

	A text is a vector of unicode code points, decoded like eTextPara::renderString decodes it. A cut k keeps the first
	k code points (0 <= k < size: k == size is the text itself, no cut). A cut is allowed if it
	 - keeps at least minKeep code points (the cursor of an Input must stay visible),
	 - doesn't separate a code point from the combining marks, joiners, variation selectors and emoji modifiers
	   behind it, nor the two halves of a flag (regional indicator pairs),
	 - doesn't fall inside an escape sequence of the text (\n \t \r \C and \c followed by 8 hex digits),
	 - doesn't leave white space (or a line feed) in front of the ellipsis; leading white space stays.
*/

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace ellipsis
{
	const unsigned int ELLIPSIS = 0x2026;
	const char *const ELLIPSIS_UTF8 = "\xE2\x80\xA6";
	const char *const DOTS_UTF8 = "...";

	/* UTF-8 to code points, the way eTextPara::renderString does it (the encoding is assumed to be valid, a stray
	   byte is taken for a code point of its own). offsets[i] is the byte offset of code point i, offsets[size] the length. */
	inline void decode(const char *string, std::vector<unsigned int> &cps, std::vector<size_t> &offsets)
	{
		cps.clear();
		offsets.clear();
		const unsigned char *start = reinterpret_cast<const unsigned char *>(string ? string : "");
		const unsigned char *p = start;
		while (*p)
		{
			offsets.push_back(p - start);
			unsigned int unicode = *p++;
			if (unicode & 0x80)
			{
				int more = 0;
				if ((unicode & 0xE0) == 0xC0)
				{
					unicode &= 0x1F;
					more = 1;
				}
				else if ((unicode & 0xF0) == 0xE0)
				{
					unicode &= 0x0F;
					more = 2;
				}
				else if ((unicode & 0xF8) == 0xF0)
				{
					unicode &= 0x07;
					more = 3;
				}
				for (int n = 0; n < more; ++n)
				{
					unicode <<= 6;
					if (*p)
						unicode |= (*p++) & 0x3F;
				}
			}
			cps.push_back(unicode);
		}
		offsets.push_back(p - start);
	}

	inline bool inRange(unsigned int cp, unsigned int first, unsigned int last)
	{
		return cp >= first && cp <= last;
	}

	/* A code point that belongs to the one in front of it: a cut in front of it would tear a glyph apart. */
	inline bool isContinuation(unsigned int cp)
	{
		if (cp < 0x300)
			return false;
		return inRange(cp, 0x0300, 0x036F)		 // combining diacritical marks
			   || inRange(cp, 0x0483, 0x0489)	 // cyrillic combining
			   || inRange(cp, 0x0591, 0x05BD)	 // hebrew points
			   || cp == 0x05BF || inRange(cp, 0x05C1, 0x05C2) || inRange(cp, 0x05C4, 0x05C5) || cp == 0x05C7
			   || inRange(cp, 0x0610, 0x061A)	 // arabic marks
			   || inRange(cp, 0x064B, 0x065F) || cp == 0x0670
			   || inRange(cp, 0x06D6, 0x06DC) || inRange(cp, 0x06DF, 0x06E4) || inRange(cp, 0x06E7, 0x06E8) || inRange(cp, 0x06EA, 0x06ED)
			   || cp == 0x0711 || inRange(cp, 0x0730, 0x074A) || inRange(cp, 0x07A6, 0x07B0)
			   || inRange(cp, 0x0900, 0x0903)	 // devanagari signs, vowel signs and virama
			   || inRange(cp, 0x093A, 0x093C) || inRange(cp, 0x093E, 0x094F) || inRange(cp, 0x0951, 0x0957) || inRange(cp, 0x0962, 0x0963)
			   || cp == 0x0E31 || inRange(cp, 0x0E34, 0x0E3A) || inRange(cp, 0x0E47, 0x0E4E) // thai
			   || cp == 0x0EB1 || inRange(cp, 0x0EB4, 0x0EBC) || inRange(cp, 0x0EC8, 0x0ECD) // lao
			   || inRange(cp, 0x1160, 0x11FF)	 // hangul vowels and final consonants
			   || inRange(cp, 0x180B, 0x180D)	 // mongolian variation selectors
			   || inRange(cp, 0x1AB0, 0x1AFF) || inRange(cp, 0x1DC0, 0x1DFF) // combining extended, supplement
			   || inRange(cp, 0x200C, 0x200D)	 // zero width non-joiner, joiner
			   || inRange(cp, 0x20D0, 0x20FF)	 // combining marks for symbols (keycap)
			   || inRange(cp, 0x3099, 0x309A)	 // combining kana voiced marks
			   || inRange(cp, 0xFE00, 0xFE0F)	 // variation selectors (text/emoji presentation)
			   || inRange(cp, 0xFE20, 0xFE2F)	 // combining half marks
			   || inRange(cp, 0xFF9E, 0xFF9F)	 // halfwidth voiced marks
			   || inRange(cp, 0x1F3FB, 0x1F3FF)	 // emoji skin tones
			   || inRange(cp, 0xE0020, 0xE007F)	 // tags (subdivision flags)
			   || inRange(cp, 0xE0100, 0xE01EF);	 // ideographic variation selectors
	}

	inline bool isRegionalIndicator(unsigned int cp)
	{
		return inRange(cp, 0x1F1E6, 0x1F1FF);
	}

	/* Nothing the ellipsis should follow: a space or a line feed (the real ones, escapes are handled in cutPoints). */
	inline bool isWhiteSpace(unsigned int cp)
	{
		return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0 || cp == 0x8A || cp == 0xE08A
			   || inRange(cp, 0x2000, 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x3000;
	}

	/* The length of the escape sequence of the text that starts at cps[i] (eTextPara::renderString reads them when it
	   isn't called with RS_DIRECT), 1 if there is none. *white is set for the ones that are white space (\n \t \r). */
	inline size_t escapeLength(const std::vector<unsigned int> &cps, size_t i, bool *white = 0)
	{
		if (white)
			*white = false;
		if (cps[i] != '\\' || i + 1 >= cps.size())
			return 1;
		switch (cps[i + 1])
		{
		case 'n':
		case 't':
		case 'r':
			if (white)
				*white = true;
			return 2;
		case 'C':
			return 2;
		case 'c':
			if (i + 10 > cps.size())
				return 1;
			for (size_t n = 0; n < 8; ++n)
			{
				unsigned char c = (unsigned char)(cps[i + 2 + n] & 0xFF);
				if (!(isxdigit(c) || (c >= ':' && c <= '?')))
					return 1;
			}
			return 10;
		}
		return 1;
	}

	/* The allowed cuts, ascending. Empty if there is none: the text can't be cut (only minKeep code points or less). */
	inline std::vector<size_t> cutPoints(const std::vector<unsigned int> &cps, size_t minKeep = 0)
	{
		std::vector<size_t> cuts;
		const size_t n = cps.size();
		bool previousWhite = false, previousJoiner = false;
		size_t regionals = 0; // regional indicators in a row in front of the current position
		size_t i = 0;
		while (i < n)
		{
			// i starts a token (a code point or an escape sequence): is a cut in front of it allowed?
			bool allowed = i >= minKeep;
			if (i > 0)
			{
				if (isContinuation(cps[i]) || previousWhite || previousJoiner)
					allowed = false;
				else if (isRegionalIndicator(cps[i]) && (regionals & 1))
					allowed = false;
			}
			if (allowed)
				cuts.push_back(i);

			bool white = false;
			size_t length = escapeLength(cps, i, &white);
			if (length > 1)
			{
				previousWhite = white;
				previousJoiner = false;
				regionals = 0;
			}
			else
			{
				previousWhite = isWhiteSpace(cps[i]);
				previousJoiner = cps[i] == 0x200D;
				regionals = isRegionalIndicator(cps[i]) ? regionals + 1 : 0;
			}
			i += length;
		}
		return cuts;
	}

	/* The index in cuts of the largest cut for which fits(cut) is true, -1 if none. fits must be true for the small cuts
	   and false for the large ones: the number of lines and the width only grow with the text. */
	template <class Fits>
	inline int searchLargest(const std::vector<size_t> &cuts, Fits fits)
	{
		int low = 0, high = (int)cuts.size() - 1, best = -1;
		while (low <= high)
		{
			int middle = low + (high - low) / 2;
			if (fits(cuts[middle]))
			{
				best = middle;
				low = middle + 1;
			}
			else
				high = middle - 1;
		}
		return best;
	}

	/* What follows the kept text: the ellipsis, three dots if the fonts have no ellipsis, nothing if the kept text ends
	   with an ellipsis already. */
	inline const char *suffixFor(const std::vector<unsigned int> &cps, size_t cut, bool fontHasEllipsis)
	{
		if (cut > 0 && cps[cut - 1] == ELLIPSIS)
			return "";
		return fontHasEllipsis ? ELLIPSIS_UTF8 : DOTS_UTF8;
	}
}

#endif
