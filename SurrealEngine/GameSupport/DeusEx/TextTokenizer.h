#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Values match the token IDs exposed by DeusExText.DeusExTextParser.
enum class DeusExTextTags : uint8_t
{
	TT_Text,
	TT_File,
	TT_Email,
	TT_Note,
	TT_EndNote,
	TT_Goal,
	TT_EndGoal,
	TT_Comment,
	TT_EndComment,
	TT_PlayerName,
	TT_PlayerFirstName,
	TT_NewPage,
	TT_CenterText,
	TT_LeftJustify,
	TT_RightJustify,
	TT_DefaultColor,
	TT_TextColor,
	TT_RevertColor,
	TT_NewParagraph,
	TT_Bold,
	TT_EndBold,
	TT_Underline,
	TT_EndUnderline,
	TT_Italics,
	TT_EndItalics,
	TT_Graphic,
	TT_Font,
	TT_Label,
	TT_OpenBracket,
	TT_CloseBracket,
	TT_None
};

struct DeusExTextTokenColor
{
	uint8_t R = 0;
	uint8_t G = 0;
	uint8_t B = 0;
	uint8_t A = 0;
};

struct DeusExTextToken
{
	DeusExTextTags Tag = DeusExTextTags::TT_None;
	std::string Text;
	std::string Name;
	DeusExTextTokenColor Color;
	std::string EmailName;
	std::string EmailSubject;
	std::string EmailFrom;
	std::string EmailTo;
	std::string EmailCC;
	std::string FileName;
	std::string FileDescription;
};

// Pure parser for Deus Ex's text markup. It deliberately has no UObject or
// package dependencies so the game module can be tested without game data.
class DeusExTextTokenizer
{
public:
	static bool Next(const std::string& source, size_t& position, const std::string& playerName, const std::string& playerFirstName, DeusExTextToken& token);

private:
	static void SkipWhitespace(const std::string& source, size_t& position);
	static void Trim(std::string& text);
	static std::string ToUpper(std::string text);
	static bool EqualsNoCase(const std::string& left, const std::string& right);
	static size_t FindNoCase(const std::string& source, const std::string& text, size_t position);
	static std::string ParseName(const std::string& payload);
	static DeusExTextTokenColor ParseColor(const std::string& payload);
	static void ParseFile(const std::string& payload, DeusExTextToken& token);
	static void ParseEmail(const std::string& payload, DeusExTextToken& token);
	static std::string ReadField(const std::string& payload, size_t& position);
	static void ConsumeBlock(const std::string& source, size_t& position, const std::string& closingTag);
};

int ReadDeusExTextPage(const std::string& text, int page, std::string& output);
