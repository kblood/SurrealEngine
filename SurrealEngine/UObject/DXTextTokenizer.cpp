#include "DXTextTokenizer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

bool DXTextTokenizer::Next(const std::string& source, size_t& position, const std::string& playerName, const std::string& playerFirstName, DXTextToken& token)
{
	token = {};
	SkipWhitespace(source, position);
	if (position >= source.size())
		return false;

	if (source[position] != '<')
	{
		size_t end = source.find('<', position);
		if (end == std::string::npos)
			end = source.size();
		token.Tag = DeusExTextTags::TT_Text;
		token.Text = source.substr(position, end - position);
		Trim(token.Text);
		position = end;
		return !token.Text.empty() || position < source.size();
	}
	if (source.compare(position, 3, "</>") == 0)
	{
		token.Tag = DeusExTextTags::TT_CloseBracket;
		token.Text = ">";
		position += 3;
		return true;
	}

	size_t tagStart = position;
	size_t tagEnd = source.find('>', tagStart + 1);
	if (tagEnd == std::string::npos)
	{
		token.Tag = DeusExTextTags::TT_Text;
		token.Text = source.substr(position);
		position = source.size();
		return true;
	}

	std::string contents = source.substr(tagStart + 1, tagEnd - tagStart - 1);
	position = tagEnd + 1;

	std::string tagName;
	std::string payload;
	if (contents == "/<" || contents == "/>")
	{
		tagName = contents;
	}
	else
	{
		size_t equals = contents.find('=');
		tagName = contents.substr(0, equals);
		if (equals != std::string::npos)
			payload = contents.substr(equals + 1);
		Trim(tagName);
	}
	tagName = ToUpper(tagName);

	if (tagName == "FILE")
	{
		token.Tag = DeusExTextTags::TT_File;
		ParseFile(payload, token);
	}
	else if (tagName == "EMAIL")
	{
		token.Tag = DeusExTextTags::TT_Email;
		ParseEmail(payload, token);
	}
	else if (tagName == "NOTE")
	{
		token.Tag = DeusExTextTags::TT_Note;
		ConsumeBlock(source, position, "</NOTE>");
	}
	else if (tagName == "/NOTE") token.Tag = DeusExTextTags::TT_EndNote;
	else if (tagName == "GOAL")
	{
		token.Tag = DeusExTextTags::TT_Goal;
		token.Name = ParseName(payload);
		ConsumeBlock(source, position, "</GOAL>");
	}
	else if (tagName == "/GOAL") token.Tag = DeusExTextTags::TT_EndGoal;
	else if (tagName == "COMMENT")
	{
		token.Tag = DeusExTextTags::TT_Comment;
		ConsumeBlock(source, position, "</COMMENT>");
	}
	else if (tagName == "/COMMENT") token.Tag = DeusExTextTags::TT_EndComment;
	else if (tagName == "PLAYERNAME")
	{
		token.Tag = DeusExTextTags::TT_PlayerName;
		token.Text = playerName;
	}
	else if (tagName == "PLAYERFIRSTNAME")
	{
		token.Tag = DeusExTextTags::TT_PlayerFirstName;
		token.Text = playerFirstName;
	}
	else if (tagName == "NP") token.Tag = DeusExTextTags::TT_NewPage;
	else if (tagName == "JC") token.Tag = DeusExTextTags::TT_CenterText;
	else if (tagName == "JL") token.Tag = DeusExTextTags::TT_LeftJustify;
	else if (tagName == "JR") token.Tag = DeusExTextTags::TT_RightJustify;
	else if (tagName == "DC")
	{
		token.Tag = DeusExTextTags::TT_DefaultColor;
		token.Color = ParseColor(payload);
	}
	else if (tagName == "C")
	{
		token.Tag = DeusExTextTags::TT_TextColor;
		token.Color = ParseColor(payload);
	}
	else if (tagName == "/C") token.Tag = DeusExTextTags::TT_RevertColor;
	else if (tagName == "P") token.Tag = DeusExTextTags::TT_NewParagraph;
	else if (tagName == "B") token.Tag = DeusExTextTags::TT_Bold;
	else if (tagName == "/B") token.Tag = DeusExTextTags::TT_EndBold;
	else if (tagName == "U") token.Tag = DeusExTextTags::TT_Underline;
	else if (tagName == "/U") token.Tag = DeusExTextTags::TT_EndUnderline;
	else if (tagName == "I") token.Tag = DeusExTextTags::TT_Italics;
	else if (tagName == "/I") token.Tag = DeusExTextTags::TT_EndItalics;
	else if (tagName == "G")
	{
		token.Tag = DeusExTextTags::TT_Graphic;
		token.Name = ParseName(payload);
	}
	else if (tagName == "F")
	{
		token.Tag = DeusExTextTags::TT_Font;
		token.Name = ParseName(payload);
	}
	else if (tagName == "L") token.Tag = DeusExTextTags::TT_Label;
	else if (tagName == "/<")
	{
		token.Tag = DeusExTextTags::TT_OpenBracket;
		token.Text = "<";
	}
	else if (tagName == "/>")
	{
		token.Tag = DeusExTextTags::TT_CloseBracket;
		token.Text = ">";
	}
	else
	{
		token.Tag = DeusExTextTags::TT_None;
	}

	return true;
}

void DXTextTokenizer::SkipWhitespace(const std::string& source, size_t& position)
{
	while (position < source.size() && (source[position] == ' ' || source[position] == '\t' || source[position] == '\r' || source[position] == '\n'))
		position++;
}

void DXTextTokenizer::Trim(std::string& text)
{
	size_t first = 0;
	while (first < text.size() && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r' || text[first] == '\n'))
		first++;
	size_t last = text.size();
	while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' || text[last - 1] == '\r' || text[last - 1] == '\n'))
		last--;
	text = text.substr(first, last - first);
}

std::string DXTextTokenizer::ToUpper(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	return text;
}

bool DXTextTokenizer::EqualsNoCase(const std::string& left, const std::string& right)
{
	return ToUpper(left) == ToUpper(right);
}

size_t DXTextTokenizer::FindNoCase(const std::string& source, const std::string& text, size_t position)
{
	if (text.empty() || text.size() > source.size())
		return std::string::npos;
	for (size_t i = position; i + text.size() <= source.size(); i++)
	{
		if (EqualsNoCase(source.substr(i, text.size()), text))
			return i;
	}
	return std::string::npos;
}

std::string DXTextTokenizer::ParseName(const std::string& payload)
{
	std::string name = payload;
	Trim(name);
	return name;
}

DXTextTokenColor DXTextTokenizer::ParseColor(const std::string& payload)
{
	DXTextTokenColor color;
	size_t position = 0;
	std::string red = ReadField(payload, position);
	std::string green = ReadField(payload, position);
	std::string blue = ReadField(payload, position);
	color.R = static_cast<uint8_t>(std::atoi(red.c_str()));
	color.G = static_cast<uint8_t>(std::atoi(green.c_str()));
	color.B = static_cast<uint8_t>(std::atoi(blue.c_str()));
	return color;
}

void DXTextTokenizer::ParseFile(const std::string& payload, DXTextToken& token)
{
	size_t position = 0;
	token.FileName = ReadField(payload, position);
	token.FileDescription = ReadField(payload, position);
}

void DXTextTokenizer::ParseEmail(const std::string& payload, DXTextToken& token)
{
	size_t position = 0;
	token.EmailName = ReadField(payload, position);
	token.EmailSubject = ReadField(payload, position);
	token.EmailFrom = ReadField(payload, position);
	token.EmailTo = ReadField(payload, position);
	token.EmailCC = ReadField(payload, position);
}

std::string DXTextTokenizer::ReadField(const std::string& payload, size_t& position)
{
	if (position > payload.size())
		return {};
	size_t end = payload.find(',', position);
	if (end == std::string::npos)
		end = payload.size();
	std::string field = payload.substr(position, end - position);
	Trim(field);
	position = end < payload.size() ? end + 1 : payload.size() + 1;
	return field;
}

void DXTextTokenizer::ConsumeBlock(const std::string& source, size_t& position, const std::string& closingTag)
{
	size_t end = FindNoCase(source, closingTag, position);
	if (end == std::string::npos)
		position = source.size();
	else
		position = end + closingTag.size();
}

int ReadDXTextPage(const std::string& text, int page, std::string& output)
{
	constexpr size_t pageSize = 239;
	if (page < 0)
	{
		output.clear();
		return 0;
	}
	size_t start = static_cast<size_t>(page) * pageSize;
	if (start >= text.size())
	{
		output.clear();
		return 0;
	}
	size_t count = std::min(pageSize, text.size() - start);
	output = text.substr(start, count);
	return static_cast<int>(count);
}
