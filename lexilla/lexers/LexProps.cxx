// Scintilla source code edit control
/** @file LexProps.cxx
 ** Lexer for properties files.
 **/
// Copyright 1998-2001 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

#include <string>
#include <string_view>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"

using namespace Lexilla;

static inline bool AtEOL(Accessor &styler, Sci_PositionU i) {
	return (styler[i] == '\n') ||
	       ((styler[i] == '\r') && (styler.SafeGetCharAt(i + 1) != '\n'));
}

static inline bool isassignchar(unsigned char ch) {
	return (ch == '=') || (ch == ':');
}

#ifdef RB_PKW
//!-start-[PropsKeywords]
static bool isprefix(const char* target, const char* prefix) {
	while (*target && *prefix) {
		if (*target != *prefix)
			return false;
		target++;
		prefix++;
	}
	if (*prefix)
		return false;
	else
		return true;
}
//!-end-[PropsKeywords]
#endif
static void ColourisePropsLine(
#ifdef RB_PKS
	char *lineBuffer, //!- const removed [PropsKeysSets]
#else
	const char *lineBuffer,
#endif
    Sci_PositionU lengthLine,
    Sci_PositionU startLine,
    Sci_PositionU endPos,
#ifdef RB_PKS
	WordList* keywordlists[], //!-add-[PropsKeysSets]
#endif
    Accessor &styler,
    bool allowInitialSpaces) {

	Sci_PositionU i = 0;
	if (allowInitialSpaces) {
		while ((i < lengthLine) && isspacechar(lineBuffer[i]))	// Skip initial spaces
			i++;
	} else {
		if (isspacechar(lineBuffer[i])) // don't allow initial spaces
			i = lengthLine;
	}

	if (i < lengthLine) {
		if (lineBuffer[i] == '#' || lineBuffer[i] == '!' || lineBuffer[i] == ';') {
			styler.ColourTo(endPos, SCE_PROPS_COMMENT);
		} else if (lineBuffer[i] == '[') {
			styler.ColourTo(endPos, SCE_PROPS_SECTION);
		} else if (lineBuffer[i] == '@') {
			styler.ColourTo(startLine + i, SCE_PROPS_DEFVAL);
			if (isassignchar(lineBuffer[i++]))
				styler.ColourTo(startLine + i, SCE_PROPS_ASSIGNMENT);
			styler.ColourTo(endPos, SCE_PROPS_DEFAULT);
#ifdef RB_PKW
			//!-start-[PropsKeywords]
		}
		else if (isprefix(lineBuffer, "import ")) {
			styler.ColourTo(startLine + 6, SCE_PROPS_KEYWORD);
			styler.ColourTo(endPos, SCE_PROPS_DEFAULT);
		}
		else if (isprefix(lineBuffer, "if ")) {
			styler.ColourTo(startLine + 2, SCE_PROPS_KEYWORD);
			styler.ColourTo(endPos, SCE_PROPS_DEFAULT);
			//!-end-[PropsKeywords]
#endif // RB_PKW

		} else {
			// Search for the '=' character
			while ((i < lengthLine) && !isassignchar(lineBuffer[i]))
				i++;
			if ((i < lengthLine) && isassignchar(lineBuffer[i])) {
#ifdef RB_PKS
				//!-start-[PropsKeysSets]
				if (i > 0) {
					int chAttr;
					lineBuffer[i] = '\0';
					// remove trailing spaces
					int indent = 0;
					while (lineBuffer[0] == ' ' || lineBuffer[0] == '\t') {
						lineBuffer++;
						indent++;
					}
					int len = 0, fin = 0;
					if ((*keywordlists[0]).InListPartly(lineBuffer, '~', len, fin)) {
						chAttr = SCE_PROPS_KEYSSET0;
					}
					else if ((*keywordlists[1]).InListPartly(lineBuffer, '~', len, fin)) {
						chAttr = SCE_PROPS_KEYSSET1;
					}
					else if ((*keywordlists[2]).InListPartly(lineBuffer, '~', len, fin)) {
						chAttr = SCE_PROPS_KEYSSET2;
					}
					else if ((*keywordlists[3]).InListPartly(lineBuffer, '~', len, fin)) {
						chAttr = SCE_PROPS_KEYSSET3;
					}
					else {
						chAttr = SCE_PROPS_KEY;
					}
					styler.ColourTo(startLine + indent + len, chAttr);
					styler.ColourTo(startLine + i - 1 - fin, SCE_PROPS_KEY);
					styler.ColourTo(startLine + i - 1, chAttr);
			}
				//!-end-[PropsKeysSets]
#else
				styler.ColourTo(startLine + i - 1, SCE_PROPS_KEY);
#endif
				styler.ColourTo(startLine + i, SCE_PROPS_ASSIGNMENT);
				styler.ColourTo(endPos, SCE_PROPS_DEFAULT);
			} else {
				styler.ColourTo(endPos, SCE_PROPS_DEFAULT);
			}
		}
	} else {
		styler.ColourTo(endPos, SCE_PROPS_DEFAULT);
	}
}

#ifdef RB_PKS
static void ColourisePropsDoc(Sci_PositionU startPos, Sci_Position length, int, WordList *keywordlists[], Accessor &styler) {
#else
static void ColourisePropsDoc(Sci_PositionU startPos, Sci_Position length, int, WordList *[], Accessor &styler) {
#endif // RB_PKS
	std::string lineBuffer;
	styler.StartAt(startPos);
	styler.StartSegment(startPos);
	Sci_PositionU startLine = startPos;

	// property lexer.props.allow.initial.spaces
	//	For properties files, set to 0 to style all lines that start with whitespace in the default style.
	//	This is not suitable for SciTE .properties files which use indentation for flow control but
	//	can be used for RFC2822 text where indentation is used for continuation lines.
	const bool allowInitialSpaces = styler.GetPropertyInt("lexer.props.allow.initial.spaces", 1) != 0;

#ifdef RB_PCF
	//!-start-[PropsColouriseFix]
	char style = 0;
	bool continuation = false;
	if (startPos >= 3)
		continuation = styler.StyleAt(startPos - 2) != SCE_PROPS_COMMENT && ((styler[startPos - 2] == '\\')
			|| (styler[startPos - 3] == '\\' && styler[startPos - 2] == '\r'));
	//!-end-[PropsColouriseFix]
#endif // RB_PCF

	for (Sci_PositionU i = startPos; i < startPos + length; i++) {
		lineBuffer.push_back(styler[i]);
		if (AtEOL(styler, i)) {
			// End of line (or of line buffer) met, colourise it
#ifdef RB_PKS
			if (continuation)
				styler.ColourTo(i, SCE_PROPS_DEFAULT);
			else
			ColourisePropsLine(lineBuffer.data(), lineBuffer.length(), startLine, i, keywordlists, styler, allowInitialSpaces);

			// test: is next a continuation of line
			//continuation = (linePos >= sizeof(lineBuffer) - 1) ||
			//	(style != SCE_PROPS_COMMENT && ((lineBuffer[linePos - 2] == '\\')
			//		|| (lineBuffer[linePos - 3] == '\\' && lineBuffer[linePos - 2] == '\r')));

#else
			ColourisePropsLine(lineBuffer.c_str(), lineBuffer.length(), startLine, i, styler, allowInitialSpaces);
#endif
			lineBuffer.clear();
			startLine = i + 1;
		}
	}
	if (lineBuffer.length() > 0) {	// Last line does not have ending characters
#ifdef RB_PCF
		//!-start-[PropsColouriseFix]
		if (continuation)
			styler.ColourTo(startPos + length - 1, SCE_PROPS_DEFAULT);
		else
			//!-end-[PropsColouriseFix]
#endif // RB_PCF
#ifdef RB_PKS
		ColourisePropsLine(lineBuffer.data(), lineBuffer.length(), startLine, startPos + length - 1, keywordlists, styler, allowInitialSpaces);
#else
		ColourisePropsLine(lineBuffer.c_str(), lineBuffer.length(), startLine, startPos + length - 1, styler, allowInitialSpaces);
#endif // RB_PKS

	}
}

// adaption by ksc, using the "} else {" trick of 1.53
// 030721
static void FoldPropsDoc(Sci_PositionU startPos, Sci_Position length, int, WordList *[], Accessor &styler) {
	const bool foldCompact = styler.GetPropertyInt("fold.compact", 1) != 0;

	const Sci_PositionU endPos = startPos + length;
	int visibleChars = 0;
	Sci_Position lineCurrent = styler.GetLine(startPos);

	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	bool headerPoint = false;
	int lev;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		const char ch = chNext;
		chNext = styler[i+1];

		const int style = styleNext;
		styleNext = styler.StyleAt(i + 1);
		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');

		if (style == SCE_PROPS_SECTION) {
			headerPoint = true;
		}

		if (atEOL) {
			lev = SC_FOLDLEVELBASE;

			if (lineCurrent > 0) {
				const int levelPrevious = styler.LevelAt(lineCurrent - 1);

				if (levelPrevious & SC_FOLDLEVELHEADERFLAG) {
					lev = SC_FOLDLEVELBASE + 1;
				} else {
					lev = levelPrevious & SC_FOLDLEVELNUMBERMASK;
				}
			}

			if (headerPoint) {
				lev = SC_FOLDLEVELBASE;
			}
			if (visibleChars == 0 && foldCompact)
				lev |= SC_FOLDLEVELWHITEFLAG;

			if (headerPoint) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}

			lineCurrent++;
			visibleChars = 0;
			headerPoint = false;
		}
		if (!isspacechar(ch))
			visibleChars++;
	}

	if (lineCurrent > 0) {
		const int levelPrevious = styler.LevelAt(lineCurrent - 1);
		if (levelPrevious & SC_FOLDLEVELHEADERFLAG) {
			lev = SC_FOLDLEVELBASE + 1;
		} else {
			lev = levelPrevious & SC_FOLDLEVELNUMBERMASK;
		}
	} else {
		lev = SC_FOLDLEVELBASE;
	}
	int flagsNext = styler.LevelAt(lineCurrent);
	styler.SetLevel(lineCurrent, lev | (flagsNext & ~SC_FOLDLEVELNUMBERMASK));
}

static const char *const emptyWordListDesc[] = {
	0
};

#ifdef RB_PKS
//!-start-[PropsKeysSets]
static const char* const propsWordListDesc[] = {
	"Keys set 0",
	"Keys set 1",
	"Keys set 2",
	"Keys set 3",
	0
};
//!-end-[PropsKeysSets]
LexerModule lmProps(SCLEX_PROPERTIES, ColourisePropsDoc, "props", FoldPropsDoc, propsWordListDesc);
#else
LexerModule lmProps(SCLEX_PROPERTIES, ColourisePropsDoc, "props", FoldPropsDoc, emptyWordListDesc);
#endif // RB_PKS

