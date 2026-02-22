"""
Lexer: tokenizes IDL source text into a stream of tokens.
"""

from dataclasses import dataclass
from typing import List

# Token kinds.
TOK_KEYWORD = "KEYWORD"
TOK_IDENT   = "IDENT"
TOK_NUMBER  = "NUMBER"
TOK_SYMBOL  = "SYMBOL"
TOK_ATTR    = "ATTR"     # e.g. [method=1], [in], [out], [notify=2]
TOK_EOF     = "EOF"

# Words treated as keywords by the parser.
KEYWORDS = {"service", "notifications", "int", "void", "enum", "struct"}


@dataclass
class Token:
    kind: str
    value: str
    line: int


def tokenize(text: str) -> List[Token]:
    """
    Convert IDL source text into a list of tokens.

    Handles: keywords, identifiers, numbers, symbols, bracketed attributes,
    single-line comments (//), block comments (/* ... */), and whitespace.
    Raises SyntaxError on unterminated constructs or unexpected characters.
    """
    tokens: List[Token] = []
    i = 0
    line = 1
    n = len(text)

    while i < n:
        # Newlines
        if text[i] == "\n":
            line += 1
            i += 1
            continue

        # Whitespace
        if text[i] in " \t\r":
            i += 1
            continue

        # Single-line comment
        if text[i:i+2] == "//":
            while i < n and text[i] != "\n":
                i += 1
            continue

        # Block comment
        if text[i:i+2] == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                raise SyntaxError(f"Line {line}: unterminated block comment")
            line += text[i:end+2].count("\n")
            i = end + 2
            continue

        # Attribute: [something] or [something=value]
        if text[i] == "[":
            j = text.find("]", i)
            if j == -1:
                raise SyntaxError(f"Line {line}: unterminated attribute")
            tokens.append(Token(TOK_ATTR, text[i+1:j].strip(), line))
            i = j + 1
            continue

        # Symbols
        if text[i] in "{}();,*=":
            tokens.append(Token(TOK_SYMBOL, text[i], line))
            i += 1
            continue

        # Number
        if text[i].isdigit():
            j = i
            while j < n and text[j].isdigit():
                j += 1
            tokens.append(Token(TOK_NUMBER, text[i:j], line))
            i = j
            continue

        # Identifier / keyword
        if text[i].isalpha() or text[i] == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            word = text[i:j]
            kind = TOK_KEYWORD if word in KEYWORDS else TOK_IDENT
            tokens.append(Token(kind, word, line))
            i = j
            continue

        raise SyntaxError(f"Line {line}: unexpected character '{text[i]}'")

    tokens.append(Token(TOK_EOF, "", line))
    return tokens
