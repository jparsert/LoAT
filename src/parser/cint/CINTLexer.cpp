
// Generated from CINT.g4 by ANTLR 4.11.1


#include "CINTLexer.h"


using namespace antlr4;



using namespace antlr4;

namespace {

struct CINTLexerStaticData final {
  CINTLexerStaticData(std::vector<std::string> ruleNames,
                          std::vector<std::string> channelNames,
                          std::vector<std::string> modeNames,
                          std::vector<std::string> literalNames,
                          std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), channelNames(std::move(channelNames)),
        modeNames(std::move(modeNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  CINTLexerStaticData(const CINTLexerStaticData&) = delete;
  CINTLexerStaticData(CINTLexerStaticData&&) = delete;
  CINTLexerStaticData& operator=(const CINTLexerStaticData&) = delete;
  CINTLexerStaticData& operator=(CINTLexerStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> channelNames;
  const std::vector<std::string> modeNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag cintlexerLexerOnceFlag;
CINTLexerStaticData *cintlexerLexerStaticData = nullptr;

void cintlexerLexerInitialize() {
  assert(cintlexerLexerStaticData == nullptr);
  auto staticData = std::make_unique<CINTLexerStaticData>(
    std::vector<std::string>{
      "INT", "IF", "WHILE", "ELSE", "TERMINATOR", "OPENP", "CLOSEP", "OPENC", 
      "CLOSEC", "MULT", "MINUS", "PLUS", "BO", "ASSIGN", "NONDETNAME", "OR", 
      "AND", "NOT", "TRUE", "FALSE", "TYPEDEF", "ENUM", "COMMA", "BOOL", 
      "EXTERN", "VOID", "MAIN", "RETURN", "ZERO", "POS", "NONZERO", "DIGIT", 
      "V", "ALPHANUM", "CHAR", "LOW", "UP", "W", "BLOCKCOMMENT", "LINECOMMENT"
    },
    std::vector<std::string>{
      "DEFAULT_TOKEN_CHANNEL", "HIDDEN"
    },
    std::vector<std::string>{
      "DEFAULT_MODE"
    },
    std::vector<std::string>{
      "", "'int'", "'if'", "'while'", "'else'", "';'", "'('", "')'", "'{'", 
      "'}'", "'*'", "'-'", "'+'", "", "'='", "'__VERIFIER_nondet_int'", 
      "'||'", "'&&'", "'!'", "'true'", "'false'", "'typedef'", "'enum'", 
      "','", "'bool'", "'extern'", "'void'", "'main'", "'return'", "'0'"
    },
    std::vector<std::string>{
      "", "INT", "IF", "WHILE", "ELSE", "TERMINATOR", "OPENP", "CLOSEP", 
      "OPENC", "CLOSEC", "MULT", "MINUS", "PLUS", "BO", "ASSIGN", "NONDETNAME", 
      "OR", "AND", "NOT", "TRUE", "FALSE", "TYPEDEF", "ENUM", "COMMA", "BOOL", 
      "EXTERN", "VOID", "MAIN", "RETURN", "ZERO", "POS", "V", "W", "BLOCKCOMMENT", 
      "LINECOMMENT"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,0,34,274,6,-1,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,
  	6,2,7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,
  	7,14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,
  	7,21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,
  	7,28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,
  	7,35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,1,0,1,0,1,0,1,0,1,1,1,1,
  	1,1,1,2,1,2,1,2,1,2,1,2,1,2,1,3,1,3,1,3,1,3,1,3,1,4,1,4,1,5,1,5,1,6,1,
  	6,1,7,1,7,1,8,1,8,1,9,1,9,1,10,1,10,1,11,1,11,1,12,1,12,1,12,1,12,1,12,
  	1,12,1,12,1,12,1,12,3,12,125,8,12,1,13,1,13,1,14,1,14,1,14,1,14,1,14,
  	1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,
  	1,14,1,14,1,14,1,15,1,15,1,15,1,16,1,16,1,16,1,17,1,17,1,18,1,18,1,18,
  	1,18,1,18,1,19,1,19,1,19,1,19,1,19,1,19,1,20,1,20,1,20,1,20,1,20,1,20,
  	1,20,1,20,1,21,1,21,1,21,1,21,1,21,1,22,1,22,1,23,1,23,1,23,1,23,1,23,
  	1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,25,1,25,1,25,1,25,1,25,1,26,1,26,
  	1,26,1,26,1,26,1,27,1,27,1,27,1,27,1,27,1,27,1,27,1,28,1,28,1,29,1,29,
  	5,29,218,8,29,10,29,12,29,221,9,29,1,30,1,30,1,31,1,31,3,31,227,8,31,
  	1,32,1,32,5,32,231,8,32,10,32,12,32,234,9,32,1,33,1,33,3,33,238,8,33,
  	1,34,1,34,3,34,242,8,34,1,35,1,35,1,36,1,36,1,37,1,37,1,38,1,38,1,38,
  	1,38,5,38,254,8,38,10,38,12,38,257,9,38,1,38,1,38,1,38,1,38,1,38,1,39,
  	1,39,1,39,1,39,5,39,268,8,39,10,39,12,39,271,9,39,1,39,1,39,1,255,0,40,
  	1,1,3,2,5,3,7,4,9,5,11,6,13,7,15,8,17,9,19,10,21,11,23,12,25,13,27,14,
  	29,15,31,16,33,17,35,18,37,19,39,20,41,21,43,22,45,23,47,24,49,25,51,
  	26,53,27,55,28,57,29,59,30,61,0,63,0,65,31,67,0,69,0,71,0,73,0,75,32,
  	77,33,79,34,1,0,3,2,0,60,60,62,62,3,0,9,10,13,13,32,32,2,0,10,10,13,13,
  	278,0,1,1,0,0,0,0,3,1,0,0,0,0,5,1,0,0,0,0,7,1,0,0,0,0,9,1,0,0,0,0,11,
  	1,0,0,0,0,13,1,0,0,0,0,15,1,0,0,0,0,17,1,0,0,0,0,19,1,0,0,0,0,21,1,0,
  	0,0,0,23,1,0,0,0,0,25,1,0,0,0,0,27,1,0,0,0,0,29,1,0,0,0,0,31,1,0,0,0,
  	0,33,1,0,0,0,0,35,1,0,0,0,0,37,1,0,0,0,0,39,1,0,0,0,0,41,1,0,0,0,0,43,
  	1,0,0,0,0,45,1,0,0,0,0,47,1,0,0,0,0,49,1,0,0,0,0,51,1,0,0,0,0,53,1,0,
  	0,0,0,55,1,0,0,0,0,57,1,0,0,0,0,59,1,0,0,0,0,65,1,0,0,0,0,75,1,0,0,0,
  	0,77,1,0,0,0,0,79,1,0,0,0,1,81,1,0,0,0,3,85,1,0,0,0,5,88,1,0,0,0,7,94,
  	1,0,0,0,9,99,1,0,0,0,11,101,1,0,0,0,13,103,1,0,0,0,15,105,1,0,0,0,17,
  	107,1,0,0,0,19,109,1,0,0,0,21,111,1,0,0,0,23,113,1,0,0,0,25,124,1,0,0,
  	0,27,126,1,0,0,0,29,128,1,0,0,0,31,150,1,0,0,0,33,153,1,0,0,0,35,156,
  	1,0,0,0,37,158,1,0,0,0,39,163,1,0,0,0,41,169,1,0,0,0,43,177,1,0,0,0,45,
  	182,1,0,0,0,47,184,1,0,0,0,49,189,1,0,0,0,51,196,1,0,0,0,53,201,1,0,0,
  	0,55,206,1,0,0,0,57,213,1,0,0,0,59,215,1,0,0,0,61,222,1,0,0,0,63,226,
  	1,0,0,0,65,228,1,0,0,0,67,237,1,0,0,0,69,241,1,0,0,0,71,243,1,0,0,0,73,
  	245,1,0,0,0,75,247,1,0,0,0,77,249,1,0,0,0,79,263,1,0,0,0,81,82,5,105,
  	0,0,82,83,5,110,0,0,83,84,5,116,0,0,84,2,1,0,0,0,85,86,5,105,0,0,86,87,
  	5,102,0,0,87,4,1,0,0,0,88,89,5,119,0,0,89,90,5,104,0,0,90,91,5,105,0,
  	0,91,92,5,108,0,0,92,93,5,101,0,0,93,6,1,0,0,0,94,95,5,101,0,0,95,96,
  	5,108,0,0,96,97,5,115,0,0,97,98,5,101,0,0,98,8,1,0,0,0,99,100,5,59,0,
  	0,100,10,1,0,0,0,101,102,5,40,0,0,102,12,1,0,0,0,103,104,5,41,0,0,104,
  	14,1,0,0,0,105,106,5,123,0,0,106,16,1,0,0,0,107,108,5,125,0,0,108,18,
  	1,0,0,0,109,110,5,42,0,0,110,20,1,0,0,0,111,112,5,45,0,0,112,22,1,0,0,
  	0,113,114,5,43,0,0,114,24,1,0,0,0,115,116,5,60,0,0,116,125,5,61,0,0,117,
  	118,5,62,0,0,118,125,5,61,0,0,119,125,7,0,0,0,120,121,5,61,0,0,121,125,
  	5,61,0,0,122,123,5,33,0,0,123,125,5,61,0,0,124,115,1,0,0,0,124,117,1,
  	0,0,0,124,119,1,0,0,0,124,120,1,0,0,0,124,122,1,0,0,0,125,26,1,0,0,0,
  	126,127,5,61,0,0,127,28,1,0,0,0,128,129,5,95,0,0,129,130,5,95,0,0,130,
  	131,5,86,0,0,131,132,5,69,0,0,132,133,5,82,0,0,133,134,5,73,0,0,134,135,
  	5,70,0,0,135,136,5,73,0,0,136,137,5,69,0,0,137,138,5,82,0,0,138,139,5,
  	95,0,0,139,140,5,110,0,0,140,141,5,111,0,0,141,142,5,110,0,0,142,143,
  	5,100,0,0,143,144,5,101,0,0,144,145,5,116,0,0,145,146,5,95,0,0,146,147,
  	5,105,0,0,147,148,5,110,0,0,148,149,5,116,0,0,149,30,1,0,0,0,150,151,
  	5,124,0,0,151,152,5,124,0,0,152,32,1,0,0,0,153,154,5,38,0,0,154,155,5,
  	38,0,0,155,34,1,0,0,0,156,157,5,33,0,0,157,36,1,0,0,0,158,159,5,116,0,
  	0,159,160,5,114,0,0,160,161,5,117,0,0,161,162,5,101,0,0,162,38,1,0,0,
  	0,163,164,5,102,0,0,164,165,5,97,0,0,165,166,5,108,0,0,166,167,5,115,
  	0,0,167,168,5,101,0,0,168,40,1,0,0,0,169,170,5,116,0,0,170,171,5,121,
  	0,0,171,172,5,112,0,0,172,173,5,101,0,0,173,174,5,100,0,0,174,175,5,101,
  	0,0,175,176,5,102,0,0,176,42,1,0,0,0,177,178,5,101,0,0,178,179,5,110,
  	0,0,179,180,5,117,0,0,180,181,5,109,0,0,181,44,1,0,0,0,182,183,5,44,0,
  	0,183,46,1,0,0,0,184,185,5,98,0,0,185,186,5,111,0,0,186,187,5,111,0,0,
  	187,188,5,108,0,0,188,48,1,0,0,0,189,190,5,101,0,0,190,191,5,120,0,0,
  	191,192,5,116,0,0,192,193,5,101,0,0,193,194,5,114,0,0,194,195,5,110,0,
  	0,195,50,1,0,0,0,196,197,5,118,0,0,197,198,5,111,0,0,198,199,5,105,0,
  	0,199,200,5,100,0,0,200,52,1,0,0,0,201,202,5,109,0,0,202,203,5,97,0,0,
  	203,204,5,105,0,0,204,205,5,110,0,0,205,54,1,0,0,0,206,207,5,114,0,0,
  	207,208,5,101,0,0,208,209,5,116,0,0,209,210,5,117,0,0,210,211,5,114,0,
  	0,211,212,5,110,0,0,212,56,1,0,0,0,213,214,5,48,0,0,214,58,1,0,0,0,215,
  	219,3,61,30,0,216,218,3,63,31,0,217,216,1,0,0,0,218,221,1,0,0,0,219,217,
  	1,0,0,0,219,220,1,0,0,0,220,60,1,0,0,0,221,219,1,0,0,0,222,223,2,49,57,
  	0,223,62,1,0,0,0,224,227,3,57,28,0,225,227,3,61,30,0,226,224,1,0,0,0,
  	226,225,1,0,0,0,227,64,1,0,0,0,228,232,3,69,34,0,229,231,3,67,33,0,230,
  	229,1,0,0,0,231,234,1,0,0,0,232,230,1,0,0,0,232,233,1,0,0,0,233,66,1,
  	0,0,0,234,232,1,0,0,0,235,238,3,69,34,0,236,238,3,63,31,0,237,235,1,0,
  	0,0,237,236,1,0,0,0,238,68,1,0,0,0,239,242,3,71,35,0,240,242,3,73,36,
  	0,241,239,1,0,0,0,241,240,1,0,0,0,242,70,1,0,0,0,243,244,2,97,122,0,244,
  	72,1,0,0,0,245,246,2,65,90,0,246,74,1,0,0,0,247,248,7,1,0,0,248,76,1,
  	0,0,0,249,250,5,47,0,0,250,251,5,42,0,0,251,255,1,0,0,0,252,254,9,0,0,
  	0,253,252,1,0,0,0,254,257,1,0,0,0,255,256,1,0,0,0,255,253,1,0,0,0,256,
  	258,1,0,0,0,257,255,1,0,0,0,258,259,5,42,0,0,259,260,5,47,0,0,260,261,
  	1,0,0,0,261,262,6,38,0,0,262,78,1,0,0,0,263,264,5,47,0,0,264,265,5,47,
  	0,0,265,269,1,0,0,0,266,268,8,2,0,0,267,266,1,0,0,0,268,271,1,0,0,0,269,
  	267,1,0,0,0,269,270,1,0,0,0,270,272,1,0,0,0,271,269,1,0,0,0,272,273,6,
  	39,0,0,273,80,1,0,0,0,9,0,124,219,226,232,237,241,255,269,1,6,0,0
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  cintlexerLexerStaticData = staticData.release();
}

}

CINTLexer::CINTLexer(CharStream *input) : Lexer(input) {
  CINTLexer::initialize();
  _interpreter = new atn::LexerATNSimulator(this, *cintlexerLexerStaticData->atn, cintlexerLexerStaticData->decisionToDFA, cintlexerLexerStaticData->sharedContextCache);
}

CINTLexer::~CINTLexer() {
  delete _interpreter;
}

std::string CINTLexer::getGrammarFileName() const {
  return "CINT.g4";
}

const std::vector<std::string>& CINTLexer::getRuleNames() const {
  return cintlexerLexerStaticData->ruleNames;
}

const std::vector<std::string>& CINTLexer::getChannelNames() const {
  return cintlexerLexerStaticData->channelNames;
}

const std::vector<std::string>& CINTLexer::getModeNames() const {
  return cintlexerLexerStaticData->modeNames;
}

const dfa::Vocabulary& CINTLexer::getVocabulary() const {
  return cintlexerLexerStaticData->vocabulary;
}

antlr4::atn::SerializedATNView CINTLexer::getSerializedATN() const {
  return cintlexerLexerStaticData->serializedATN;
}

const atn::ATN& CINTLexer::getATN() const {
  return *cintlexerLexerStaticData->atn;
}




void CINTLexer::initialize() {
  ::antlr4::internal::call_once(cintlexerLexerOnceFlag, cintlexerLexerInitialize);
}