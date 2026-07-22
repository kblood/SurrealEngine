#include "UObject/DXTextTokenizer.h"

#include <iostream>
#include <string>
#include <vector>

static int failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		failures++;
	}
}

static DXTextToken Next(const std::string& source, size_t& position, const std::string& message)
{
	DXTextToken token;
	Check(DXTextTokenizer::Next(source, position, "JC Denton", "JC", token), message + " should produce a token");
	return token;
}

static void TestStockDocument()
{
	const std::string source = "<DC=255,127,3><P><JC><B>0012</B><P>Done";
	size_t position = 0;
	DXTextToken token = Next(source, position, "default color");
	Check(token.Tag == DeusExTextTags::TT_DefaultColor, "DC tag");
	Check(token.Color.R == 255 && token.Color.G == 127 && token.Color.B == 3 && token.Color.A == 0, "DC color channels");
	Check(Next(source, position, "paragraph").Tag == DeusExTextTags::TT_NewParagraph, "P tag");
	Check(Next(source, position, "center").Tag == DeusExTextTags::TT_CenterText, "JC tag");
	Check(Next(source, position, "bold").Tag == DeusExTextTags::TT_Bold, "B tag");
	token = Next(source, position, "body text");
	Check(token.Tag == DeusExTextTags::TT_Text && token.Text == "0012", "body text token");
	Check(Next(source, position, "end bold").Tag == DeusExTextTags::TT_EndBold, "/B tag");
	Check(Next(source, position, "second paragraph").Tag == DeusExTextTags::TT_NewParagraph, "second P tag");
	token = Next(source, position, "final text");
	Check(token.Tag == DeusExTextTags::TT_Text && token.Text == "Done", "text at exact EOF");
	Check(position == source.size(), "position reaches exact EOF");
	Check(!DXTextTokenizer::Next(source, position, "", "", token), "EOF does not produce a token");
}

static void TestMetadata()
{
	size_t position = 0;
	DXTextToken token = Next("<FILE=01_Bulletin01, Terrorism -- Crime or Conscience?>", position, "file");
	Check(token.Tag == DeusExTextTags::TT_File, "FILE tag");
	Check(token.FileName == "01_Bulletin01", "FILE name");
	Check(token.FileDescription == "Terrorism -- Crime or Conscience?", "FILE description trim");

	position = 0;
	token = Next("<EMAIL=01_Email10,bad nee,GHermann,JReyes>", position, "email without CC");
	Check(token.EmailName == "01_Email10" && token.EmailSubject == "bad nee", "EMAIL identity fields");
	Check(token.EmailFrom == "GHermann" && token.EmailTo == "JReyes" && token.EmailCC.empty(), "EMAIL optional CC");

	position = 0;
	token = Next("<EMAIL=01_Email03,Skul-gun,GHermann,JManderley,ANavarre,>", position, "email with extra field");
	Check(token.EmailCC == "ANavarre", "EMAIL reads five fields and ignores extras");

	position = 0;
	token = Next("<EMAIL=>", position, "empty email");
	Check(token.Tag == DeusExTextTags::TT_Email && token.EmailName.empty() && token.EmailCC.empty(), "empty EMAIL remains valid");
}

static void TestBlocksAndSubstitutions()
{
	const std::string source = "<COMMENT>hidden <B>text</B></COMMENT><P><PLAYERNAME>/<PLAYERFIRSTNAME>";
	size_t position = 0;
	DXTextToken token = Next(source, position, "comment");
	Check(token.Tag == DeusExTextTags::TT_Comment, "COMMENT tag");
	Check(Next(source, position, "after comment").Tag == DeusExTextTags::TT_NewParagraph, "COMMENT consumes its block");
	token = Next(source, position, "player name");
	Check(token.Tag == DeusExTextTags::TT_PlayerName && token.Text == "JC Denton", "PLAYERNAME substitution");
	token = Next(source, position, "separator");
	Check(token.Tag == DeusExTextTags::TT_Text && token.Text == "/", "text between substitutions");
	token = Next(source, position, "first name");
	Check(token.Tag == DeusExTextTags::TT_PlayerFirstName && token.Text == "JC", "PLAYERFIRSTNAME substitution");

	position = 0;
	token = Next("<GOAL=PrimaryGoal>ignored</GOAL><P>", position, "goal");
	Check(token.Tag == DeusExTextTags::TT_Goal && token.Name == "PrimaryGoal", "GOAL name");
	Check(Next("<GOAL=PrimaryGoal>ignored</GOAL><P>", position, "after goal").Tag == DeusExTextTags::TT_NewParagraph, "GOAL consumes its block");
}

static void TestCompleteTokenTable()
{
	struct Expected { const char* Source; DeusExTextTags Tag; };
	const std::vector<Expected> expected =
	{
		{"<NOTE>x</NOTE>", DeusExTextTags::TT_Note}, {"</NOTE>", DeusExTextTags::TT_EndNote},
		{"</GOAL>", DeusExTextTags::TT_EndGoal}, {"</COMMENT>", DeusExTextTags::TT_EndComment},
		{"<NP>", DeusExTextTags::TT_NewPage}, {"<jl>", DeusExTextTags::TT_LeftJustify},
		{"<JR>", DeusExTextTags::TT_RightJustify}, {"<C=1,2,3>", DeusExTextTags::TT_TextColor},
		{"</C>", DeusExTextTags::TT_RevertColor}, {"<U>", DeusExTextTags::TT_Underline},
		{"</U>", DeusExTextTags::TT_EndUnderline}, {"<I>", DeusExTextTags::TT_Italics},
		{"</I>", DeusExTextTags::TT_EndItalics}, {"<G=Logo>", DeusExTextTags::TT_Graphic},
		{"<F=FontMenuSmall>", DeusExTextTags::TT_Font}, {"<L=Start>", DeusExTextTags::TT_Label},
		{"</<>", DeusExTextTags::TT_OpenBracket}, {"</>", DeusExTextTags::TT_CloseBracket}
	};
	for (const Expected& item : expected)
	{
		size_t position = 0;
		DXTextToken token = Next(item.Source, position, item.Source);
		Check(token.Tag == item.Tag, std::string("token table entry ") + item.Source);
	}

	size_t position = 0;
	DXTextToken graphic = Next("<G=Logo>", position, "graphic name");
	Check(graphic.Name == "Logo", "graphic name payload");
	position = 0;
	DXTextToken font = Next("<F=FontMenuSmall>", position, "font name");
	Check(font.Name == "FontMenuSmall", "font name payload");
	position = 0;
	DXTextToken brackets = Next("</<>text</>", position, "open bracket");
	Check(brackets.Text == "<", "open bracket substitution");
	Check(Next("</<>text</>", position, "bracket body").Text == "text", "bracket body");
	Check(Next("</<>text</>", position, "close bracket").Text == ">", "close bracket substitution");
}

static void TestTextPaging()
{
	std::string source;
	for (int i = 0; i < 500; i++)
		source.push_back(static_cast<char>('A' + (i % 26)));
	std::string page;
	Check(ReadDXTextPage(source, 0, page) == 239 && page == source.substr(0, 239), "first 239-character page");
	Check(ReadDXTextPage(source, 1, page) == 239 && page == source.substr(239, 239), "second 239-character page");
	Check(ReadDXTextPage(source, 2, page) == 22 && page == source.substr(478), "short final page");
	Check(ReadDXTextPage(source, 3, page) == 0 && page.empty(), "page after EOF");
	Check(ReadDXTextPage(std::string(239, 'x'), 0, page) == 239, "exact page size");
	Check(ReadDXTextPage(std::string(239, 'x'), 1, page) == 0 && page.empty(), "exact page boundary EOF");
	Check(ReadDXTextPage("", 0, page) == 0 && page.empty(), "empty text page");
	Check(ReadDXTextPage(source, -1, page) == 0 && page.empty(), "negative page");
}

int main()
{
	TestStockDocument();
	TestMetadata();
	TestBlocksAndSubstitutions();
	TestCompleteTokenTable();
	TestTextPaging();
	if (failures == 0)
		std::cout << "All Deus Ex text tokenizer tests passed.\n";
	return failures == 0 ? 0 : 1;
}
