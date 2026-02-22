"""
Parser: recursive-descent parser that builds an AST from a token stream.
"""

import re
from dataclasses import dataclass, field
from typing import List, Optional

from .lexer import Token, TOK_KEYWORD, TOK_IDENT, TOK_NUMBER, TOK_SYMBOL, TOK_ATTR, TOK_EOF
from .types import TYPE_MAP


# ── AST nodes ────────────────────────────────────────────────────────

@dataclass
class EnumValue:
    name: str
    value: int


@dataclass
class EnumDef:
    name: str
    values: List['EnumValue']


@dataclass
class StructField:
    type_name: str   # IDL type (e.g. "uint32", "DeviceType")
    name: str
    array_size: Optional[int] = None  # e.g. 6 for uint8[6]


@dataclass
class StructDef:
    name: str
    fields: List['StructField']


@dataclass
class Param:
    direction: str   # "in" or "out"
    type_name: str   # IDL type (e.g. "uint32")
    name: str
    is_pointer: bool # True for [out] params
    array_size: Optional[int] = None  # e.g. 16 for uint8[16]


@dataclass
class Method:
    name: str
    method_id: int
    params: List[Param]


@dataclass
class Notification:
    name: str
    notify_id: int
    params: List[Param]


@dataclass
class IdlFile:
    service_name: str
    enums: List[EnumDef] = field(default_factory=list)
    structs: List[StructDef] = field(default_factory=list)
    methods: List[Method] = field(default_factory=list)
    notifications: List[Notification] = field(default_factory=list)


# ── Parser ───────────────────────────────────────────────────────────

class Parser:
    """
    Recursive-descent parser for the ms-ipc IDL.

    Expects a token list produced by ``tokenize()``.  Builds an ``IdlFile``
    AST containing ``Method`` and ``Notification`` nodes.
    """

    def __init__(self, tokens: List[Token]):
        self.tokens = tokens
        self.pos = 0
        self._user_types: set = set()

    # ── Token helpers ────────────────────────────────────────────────

    def peek(self) -> Token:
        return self.tokens[self.pos]

    def advance(self) -> Token:
        tok = self.tokens[self.pos]
        self.pos += 1
        return tok

    def expect(self, kind: str, value: Optional[str] = None) -> Token:
        tok = self.advance()
        if tok.kind != kind:
            raise SyntaxError(
                f"Line {tok.line}: expected {kind}"
                f"{f' {value!r}' if value else ''}, got {tok.kind} {tok.value!r}")
        if value is not None and tok.value != value:
            raise SyntaxError(
                f"Line {tok.line}: expected {value!r}, got {tok.value!r}")
        return tok

    # ── Top-level ────────────────────────────────────────────────────

    def parse(self) -> IdlFile:
        idl = IdlFile(service_name="")

        while self.peek().kind != TOK_EOF:
            tok = self.peek()
            if tok.kind == TOK_KEYWORD and tok.value == "enum":
                idl.enums.append(self._parse_enum())
            elif tok.kind == TOK_KEYWORD and tok.value == "struct":
                idl.structs.append(self._parse_struct())
            elif tok.kind == TOK_KEYWORD and tok.value == "service":
                self._parse_service(idl)
            elif tok.kind == TOK_KEYWORD and tok.value == "notifications":
                self._parse_notifications(idl)
            else:
                raise SyntaxError(
                    f"Line {tok.line}: expected 'enum', 'struct', 'service', "
                    f"or 'notifications', got {tok.value!r}")

        if not idl.service_name:
            raise SyntaxError("No service block found")

        return idl

    # ── Blocks ───────────────────────────────────────────────────────

    def _parse_service(self, idl: IdlFile):
        self.expect(TOK_KEYWORD, "service")
        name_tok = self.expect(TOK_IDENT)
        if idl.service_name and idl.service_name != name_tok.value:
            raise SyntaxError(
                f"Line {name_tok.line}: service name mismatch: "
                f"{name_tok.value!r} vs {idl.service_name!r}")
        idl.service_name = name_tok.value

        self.expect(TOK_SYMBOL, "{")
        while self.peek().value != "}":
            idl.methods.append(self._parse_method())
        self.expect(TOK_SYMBOL, "}")
        self.expect(TOK_SYMBOL, ";")

    def _parse_notifications(self, idl: IdlFile):
        self.expect(TOK_KEYWORD, "notifications")
        name_tok = self.expect(TOK_IDENT)
        if idl.service_name and idl.service_name != name_tok.value:
            raise SyntaxError(
                f"Line {name_tok.line}: notifications name mismatch: "
                f"{name_tok.value!r} vs {idl.service_name!r}")
        idl.service_name = name_tok.value

        self.expect(TOK_SYMBOL, "{")
        while self.peek().value != "}":
            idl.notifications.append(self._parse_notification())
        self.expect(TOK_SYMBOL, "}")
        self.expect(TOK_SYMBOL, ";")

    # ── Enum / struct ──────────────────────────────────────────────────

    def _parse_enum(self) -> EnumDef:
        self.expect(TOK_KEYWORD, "enum")
        name_tok = self.expect(TOK_IDENT)

        if name_tok.value in self._user_types or name_tok.value in TYPE_MAP:
            raise SyntaxError(
                f"Line {name_tok.line}: type {name_tok.value!r} already defined")

        self.expect(TOK_SYMBOL, "{")
        values: List[EnumValue] = []
        while self.peek().value != "}":
            val_name_tok = self.expect(TOK_IDENT)
            self.expect(TOK_SYMBOL, "=")
            val_num_tok = self.expect(TOK_NUMBER)
            values.append(EnumValue(name=val_name_tok.value,
                                    value=int(val_num_tok.value)))
            if self.peek().value == ",":
                self.advance()  # consume optional trailing comma
        self.expect(TOK_SYMBOL, "}")
        self.expect(TOK_SYMBOL, ";")

        self._user_types.add(name_tok.value)
        return EnumDef(name=name_tok.value, values=values)

    def _parse_struct(self) -> StructDef:
        self.expect(TOK_KEYWORD, "struct")
        name_tok = self.expect(TOK_IDENT)

        if name_tok.value in self._user_types or name_tok.value in TYPE_MAP:
            raise SyntaxError(
                f"Line {name_tok.line}: type {name_tok.value!r} already defined")

        self.expect(TOK_SYMBOL, "{")
        fields: List[StructField] = []
        while self.peek().value != "}":
            type_tok = self.advance()
            if type_tok.value not in TYPE_MAP and type_tok.value not in self._user_types:
                raise SyntaxError(
                    f"Line {type_tok.line}: unknown type {type_tok.value!r}")
            array_size = None
            if self.peek().kind == TOK_ATTR and self.peek().value.strip().isdigit():
                array_size = int(self.advance().value.strip())
                if array_size < 1:
                    raise SyntaxError(
                        f"Line {type_tok.line}: array size must be >= 1")
            if type_tok.value == "string" and array_size is None:
                raise SyntaxError(
                    f"Line {type_tok.line}: 'string' requires a size, "
                    f"e.g. string[64]")
            field_name_tok = self.expect(TOK_IDENT)
            self.expect(TOK_SYMBOL, ";")
            fields.append(StructField(type_name=type_tok.value,
                                      name=field_name_tok.value,
                                      array_size=array_size))
        self.expect(TOK_SYMBOL, "}")
        self.expect(TOK_SYMBOL, ";")

        if not fields:
            raise SyntaxError(
                f"Line {name_tok.line}: struct {name_tok.value!r} has no fields")

        self._user_types.add(name_tok.value)
        return StructDef(name=name_tok.value, fields=fields)

    # ── Methods / notifications ──────────────────────────────────────

    def _parse_method(self) -> Method:
        attr_tok = self.expect(TOK_ATTR)
        m = re.match(r"method\s*=\s*(\d+)", attr_tok.value)
        if not m:
            raise SyntaxError(
                f"Line {attr_tok.line}: expected [method=N], got [{attr_tok.value}]")

        self.expect(TOK_KEYWORD, "int")
        name_tok = self.expect(TOK_IDENT)
        params = self._parse_params()
        self.expect(TOK_SYMBOL, ";")

        return Method(name=name_tok.value, method_id=int(m.group(1)), params=params)

    def _parse_notification(self) -> Notification:
        attr_tok = self.expect(TOK_ATTR)
        m = re.match(r"notify\s*=\s*(\d+)", attr_tok.value)
        if not m:
            raise SyntaxError(
                f"Line {attr_tok.line}: expected [notify=N], got [{attr_tok.value}]")

        self.expect(TOK_KEYWORD, "void")
        name_tok = self.expect(TOK_IDENT)
        params = self._parse_params()
        self.expect(TOK_SYMBOL, ";")

        for p in params:
            if p.direction != "in":
                raise SyntaxError(
                    f"Line {attr_tok.line}: notification params must be [in]")

        return Notification(name=name_tok.value, notify_id=int(m.group(1)),
                            params=params)

    # ── Parameters ───────────────────────────────────────────────────

    def _parse_params(self) -> List[Param]:
        self.expect(TOK_SYMBOL, "(")
        params: List[Param] = []
        if self.peek().value != ")":
            params.append(self._parse_param())
            while self.peek().value == ",":
                self.advance()
                params.append(self._parse_param())
        self.expect(TOK_SYMBOL, ")")
        return params

    def _parse_param(self) -> Param:
        attr_tok = self.expect(TOK_ATTR)
        direction = attr_tok.value.strip()
        if direction not in ("in", "out"):
            raise SyntaxError(
                f"Line {attr_tok.line}: expected [in] or [out], got [{direction}]")

        type_tok = self.advance()
        if type_tok.value not in TYPE_MAP and type_tok.value not in self._user_types:
            raise SyntaxError(
                f"Line {type_tok.line}: unknown type {type_tok.value!r}")

        array_size = None
        if self.peek().kind == TOK_ATTR and self.peek().value.strip().isdigit():
            array_size = int(self.advance().value.strip())
            if array_size < 1:
                raise SyntaxError(
                    f"Line {type_tok.line}: array size must be >= 1")

        if type_tok.value == "string" and array_size is None:
            raise SyntaxError(
                f"Line {type_tok.line}: 'string' requires a size, "
                f"e.g. string[64]")

        name_tok = self.expect(TOK_IDENT)

        # [out] implies pointer — no * needed in IDL syntax.
        is_pointer = (direction == "out")

        return Param(direction=direction, type_name=type_tok.value,
                     name=name_tok.value, is_pointer=is_pointer,
                     array_size=array_size)
