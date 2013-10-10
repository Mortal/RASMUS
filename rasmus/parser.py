from lexer import *
from AST import *

thingsThatMayComeAfterParseExp = [TK_RIGHTARROW, TK_RPAREN, TK_COMMA, TK_FI,
                                  TK_PIPE, TK_COLON, TK_END, TK_IN, TK_CHOICE,
                                  TK_VAL, TK_TWO_DOTS, TK_EOF, TK_SEMICOLON]

class ParserException(Exception):
    def __init__(self, hat, TK):
        self.TK = TK
        Exception.__init__(self, hat)

class RecoverException(Exception):
    pass

class RecoverEOF(RecoverException):
    TK=TK_EOF

class RecoverBLOCKEND(RecoverException):
    TK=TK_BLOCKEND

class RecoverIN(RecoverException):
    TK=TK_IN

class RecoverVAL(RecoverException):
    TK=TK_VAL

class RecoverRPAREN(RecoverException):
    TK=TK_RPAREN

class RecoverSEMICOLON(RecoverException):
    TK=TK_SEMICOLON

class RecoverRBRACKET(RecoverException):
    TK=TK_RBRACKET

class RecoverCHOICE(RecoverException):
    TK=TK_CHOICE

class RecoverFI(RecoverException):
    TK=TK_FI

class RecoverEND(RecoverException):
    TK=TK_END

class Parser:
    def __init__(self, error, code):
        self.error = error
        self.code = code
        self.lexer = Lexer(code)
        self.currentToken = self.lexer.getNext()
        self.recoverStack = []
        
    def consumeToken(self):
        token = self.currentToken
        self.currentToken = self.lexer.getNext()
        return token

    def pushRecover(self, rec):
        #print "push ", tokenNames[rec.TK]
        self.recoverStack.append(rec)

    def popRecover(self, rec):
        if rec != self.recoverStack[-1]:
            print "found the error!"
        #print "pop ", tokenNames[rec.TK]
        self.recoverStack.pop()

    def recover(self):
        """Recover errors at the first token in list of tokens specified on the recovery stack"""
        r = dict( (l.TK, l) for l in self.recoverStack)
        #print "recovering " , self.recoverStack
        while not self.currentToken[0] in r:
            if self.currentToken[0] == TK_ERR:
                self.parseError("Invalid token")
            self.consumeToken()
        #print "Recover to ",r[self.currentToken[0]]
        raise r[self.currentToken[0]]()
        
    def parseError(self, error):
        TK=self.currentToken
        self.error.reportError("Parse error: %s" %error,
                               self.currentToken)
                
    def parseType(self):
        cToken = self.currentToken[0]
        if cToken in [TK_TYPE_BOOL,TK_TYPE_INT,
                      TK_TYPE_TEXT,TK_TYPE_ATOM,
                      TK_TYPE_TUP,TK_TYPE_REL,
                      TK_TYPE_FUNC,TK_TYPE_ANY]:
            return self.consumeToken()
        self.parseError("Expected Type");
        self.recover()

    def parseAssignment(self):
        nameToken = self.consumeToken()
        if self.currentToken[0] == TK_ASSIGN:
            return AssignmentExp(
                nameToken,
                self.consumeToken(),
                self.parseExp())
        else:
            return VariableExp(nameToken)

    def parseIfExp(self):
        n = IfExp(self.assertTokenConsume(TK_IF))
        self.pushRecover(RecoverFI)
        try:
            self.pushRecover(RecoverCHOICE)
            try:
                n.choices.append(Choice(
                        self.parseExp(),
                        self.assertTokenConsume(TK_RIGHTARROW),
                        self.parseExp()))
            except RecoverCHOICE:
                pass
            finally:
                self.popRecover(RecoverCHOICE)
            while self.currentToken[0] == TK_CHOICE:
                self.consumeToken()
                self.pushRecover(RecoverCHOICE)
                try:
                    n.choices.append(Choice(
                            self.parseExp(),
                            self.assertTokenConsume(TK_RIGHTARROW),
                            self.parseExp()))
                except RecoverCHOICE:
                    pass
                finally:
                    self.popRecover(RecoverCHOICE)
            self.assertToken(TK_FI)
        except RecoverFI:
            pass
        finally:
            self.popRecover(RecoverFI)
        n.fiToken = self.assertTokenConsume(TK_FI)
        return n

    def parseForallExp(self):
        n = ForallExp(
            self.consumeToken(),
            self.assertTokenConsume(TK_LPAREN),
            )
        self.pushRecover(RecoverRPAREN)
        try:
            n.listExps.append(self.parseExp())
            while self.currentToken[0] == TK_COMMA:
                self.consumeToken()
                n.listExps.append(self.parseExp())
            self.assertToken(TK_RPAREN)
        except RecoverRPAREN:
            pass
        finally:
            self.popRecover(RecoverRPAREN)

        n.rparenToken = self.assertTokenConsume(TK_RPAREN)
        if self.currentToken[0] == TK_PIPE:
            n.pipeToken = self.consumeToken()
            n.names.append(self.assertTokenConsume(TK_NAME))
            while self.currentToken[0] == TK_COMMA:
                self.consumeToken()
                n.names.append(self.assertTokenConsume(TK_NAME))
        n.colonToken = self.assertTokenConsume(TK_COLON)
        n.exp = self.parseExp()
        return n

    def parseBuiltIn(self):
        n = BuiltInExp(self.consumeToken(),
                       self.assertTokenConsume(TK_LPAREN))
        self.pushRecover(RecoverRPAREN)
        try:
            n.args.append(self.parseExp())
            while self.currentToken[0] == TK_COMMA:
                self.consumeToken()
                n.args.append(self.parseExp())
            self.assertToken(TK_RPAREN)
        except RecoverRPAREN:
            pass
        finally:
            self.popRecover(RecoverRPAREN)
        n.rparenToken = self.assertTokenConsume(TK_RPAREN)
        return n
    
    def parseFuncExp(self):
        n = FuncExp(self.consumeToken(),self.assertTokenConsume(TK_LPAREN))
        self.pushRecover(RecoverRPAREN);
        try:
            if self.currentToken[0] != TK_RPAREN:
                n.args.append(FuncArg(
                        self.assertTokenConsume(TK_NAME),
                        self.assertTokenConsume(TK_COLON),
                        self.parseType()))
                while self.currentToken[0] == TK_COMMA:
                    self.consumeToken()
                    n.args.append(FuncArg(
                            self.assertTokenConsume(TK_NAME),
                            self.assertTokenConsume(TK_COLON),
                            self.parseType()))
            self.assertToken(TK_RPAREN)
        except RecoverRPAREN:
            pass
        finally:
            self.popRecover(RecoverRPAREN)
        n.rparenToken = self.consumeToken()
        n.arrowToken = self.assertTokenConsume(TK_RIGHTARROW)
        n.lparenToken2 = self.assertTokenConsume(TK_LPAREN)
        n.returnTypeToken = self.parseType()
        n.rparenToken2 = self.assertTokenConsume(TK_RPAREN)
        self.pushRecover(RecoverEND)
        try:
            n.body = self.parseExp()
        except RecoverEND:
            pass
        finally:
            self.popRecover(RecoverEND)
        n.endToken = self.assertTokenConsume(TK_END)
        return n

    def parseTupExp(self):
        n = TupExp(self.consumeToken(), self.assertTokenConsume(TK_LPAREN))
        self.pushRecover(RecoverRPAREN)
        try:
            if self.currentToken[0] != TK_RPAREN:
                n.items.append(TupItem(
                        self.assertTokenConsume(TK_NAME),
                        self.assertTokenConsume(TK_COLON),
                        self.parseExp()))
                while self.currentToken[0] == TK_COMMA:
                    self.consumeToken()
                    n.items.append(TupItem(
                            self.assertTokenConsume(TK_NAME),
                            self.assertTokenConsume(TK_COLON),
                            self.parseExp()))
                self.assertToken(TK_RPAREN)
        except RecoverRPAREN:
            pass
        finally:
            self.popRecover(RecoverRPAREN)
        n.rparenToken = self.consumeToken()
        return n

    def parseBlockExp(self):
        n = BlockExp(self.consumeToken())
        self.pushRecover(RecoverBLOCKEND)
        try: 
            self.pushRecover(RecoverIN)                
            try:
                while self.currentToken[0] == TK_VAL:
                    valToken = self.consumeToken()
                    self.pushRecover(RecoverVAL)
                    try:
                        n.vals.append(Val(
                                valToken,
                                self.assertTokenConsume(TK_NAME), 
                                self.assertTokenConsume(TK_EQUAL),
                                self.parseExp()))
                    except RecoverVAL:
                        pass
                    finally:
                        self.popRecover(RecoverVAL)
                self.assertToken(TK_IN)
            except RecoverIN:
                pass
            finally:
                self.popRecover(RecoverIN)
            n.inToken = self.consumeToken()
            n.inExp = self.parseExp()
            self.assertToken(TK_BLOCKEND)
        except RecoverBLOCKEND:
            pass
        finally:
            self.popRecover(RecoverBLOCKEND)
        n.blockendToken = self.consumeToken()
        return n

    def parseParenthesisExp(self):
        n = None
        self.consumeToken()
        self.pushRecover(RecoverRPAREN)
        try:
            n = self.parseExp()
            self.assertToken(TK_RPAREN)
        except RecoverRPAREN:
            n = ConstantExp(None)
            pass
        finally:
            self.popRecover(RecoverRPAREN)
        self.assertTokenConsume(TK_RPAREN)
        return n

    def parseRelExp(self):
        n = RelExp(self.consumeToken(), self.assertTokenConsume(TK_LPAREN))
        self.pushRecover(RecoverRPAREN)
        try:
            n.exp = self.parseExp()
            self.assertToken(TK_RPAREN)
        except RecoverRPAREN:
            pass
        finally:
            self.popRecover(RecoverRPAREN)
        n.rparenToken = self.consumeToken()
        return n

    def parseAtExp(self):
        n = AtExp(self.consumeToken(), self.assertTokenConsume(TK_LPAREN))
        self.pushRecover(RecoverRPAREN)
        try:
            n.exp = self.parseExp()
            self.assertToken(TK_RPAREN)
        except RecoverRPAREN:
            pass
        finally:
            self.popRecover(RecoverRPAREN)
        n.rparenToken = self.consumeToken()
        return n

    def parseBottomExp(self):
        cToken = self.currentToken[0]
        if False:
            pass
        elif cToken == TK_IF:
            return self.parseIfExp()
        elif cToken == TK_LPAREN:
            return self.parseParenthesisExp()
        elif cToken in [TK_TODAY, TK_CLOSE]:
            return BuiltInExp(self.consumeToken(), None)
        elif cToken in [TK_BANG, TK_BANGLT, TK_BANGGT]:
            return self.parseForallExp()
        elif cToken in [TK_ISATOM, TK_ISTUP, TK_ISREL, 
                        TK_ISFUNC, TK_ISANY, TK_SYSTEM, 
                        TK_WRITE, TK_OPEN, TK_HAS, TK_MAX,
                        TK_MIN, TK_COUNT, TK_ADD, TK_MULT,
                        TK_DAYS, TK_BEFORE, TK_AFTER, TK_DATE,
                        TK_ISBOOL, TK_ISINT, TK_ISTEXT]:
            return self.parseBuiltIn()
        elif cToken == TK_PIPE:
            return LenExp(self.consumeToken(), self.parseExp(), self.assertTokenConsume(TK_PIPE))
        elif cToken == TK_MINUS:
            return UnaryOpExp(self.consumeToken(), self.parseExp())
        elif cToken == TK_NAME:
            return self.parseAssignment()
        elif cToken == TK_FUNC:
            return self.parseFuncExp()
        elif cToken == TK_REL:
            return self.parseRel()
        elif cToken == TK_TUP:
            return self.parseTupExp()
        elif cToken in [TK_ZERO, TK_ONE, TK_STDBOOL, TK_STDINT, TK_STDTEXT,
                        TK_TEXT, TK_INT, TK_TRUE,TK_FALSE]:
            return ConstantExp(self.consumeToken())
        elif cToken in [TK_SHARP]:
            return VariableExp(self.consumeToken())
        elif cToken == TK_BLOCKSTART:
            return self.parseBlockExp()
        elif cToken == TK_AT:
            return self.parseAtExp()
        elif cToken == TK_NOT:
            return UnaryOpExp(self.consumeToken(), self.parseExp())
        else:
            self.parseError("Unexpected token")
            self.recover()

    def parseSubstringOrFuncInvocationExp(self):
        n = self.parseBottomExp()
        if self.currentToken[0] == TK_LPAREN:
            lparenToken = self.consumeToken()

            if self.currentToken[0] == TK_RPAREN:
                n = FuncInvocationExp(n, lparenToken)
            else:
                self.pushRecover(RecoverRPAREN)
                try:
                    e1 = self.parseExp()
                    if self.currentToken[0] == TK_TWO_DOTS:
                        n = SubstringExp(n, lparenToken, e1, self.consumeToken(), self.parseExp())
                    else:
                        n = FuncInvocationExp(n, lparenToken)
                        n.args.append(e1)
                        while self.currentToken[0] == TK_COMMA:
                            self.consumeToken()
                            n.args.append(self.parseExp())
                        self.assertToken(TK_RPAREN)
                except RecoverRPAREN:
                    pass
                finally:
                    self.popRecover(RecoverRPAREN)
            n.rparenToken = self.assertTokenConsume(TK_RPAREN)
        return n

    def parseRenameExp(self):
        n =self.parseSubstringOrFuncInvocationExp()
        while self.currentToken[0] == TK_LBRACKET:
            self.pushRecover(RecoverRBRACKET)
            n = RenameExp(n, self.consumeToken())
            try:
                n.renames.append(RenameItem(self.assertTokenConsume(TK_NAME),
                                            self.assertTokenConsume(TK_LEFT_ARROW),
                                            self.assertTokenConsume(TK_NAME)))

                while self.currentToken[0] == TK_COMMA:
                    self.consumeToken()
                    n.renames.append(RenameItem(self.assertTokenConsume(TK_NAME),
                                                self.assertTokenConsume(TK_LEFT_ARROW),
                                                self.assertTokenConsume(TK_NAME)))
                self.assertToken(TK_RBRACKET)
            except RecoverRBRACKET:
                pass
            finally:
                self.popRecover(RecoverRBRACKET)
            n.rbracketToken = self.assertTokenConsume(TK_RBRACKET)
        return n

    def parseDotExp(self):
        n = self.parseRenameExp()
        if self.currentToken[0] == TK_ONE_DOT:
            n = DotExp(n, self.consumeToken(), self.assertTokenConsume(TK_NAME))
        return n

    def parseOpExtendAndOverwriteExp(self):
        n = self.parseDotExp()
        while self.currentToken[0] == TK_OPEXTEND:
            n = BinaryOpExp(self.consumeToken(), n, self.parseDotExp())
        return n

    def parseConcatExp(self):
        n = self.parseOpExtendAndOverwriteExp()
        while self.currentToken[0] == TK_CONCAT:
            n = BinaryOpExp(self.consumeToken(), n, self.parseOpExtendAndOverwriteExp())
        return n

    def parseProjectionExp(self):
        n = self.parseConcatExp()
        while self.currentToken[0] in [TK_PROJECT_PLUS, TK_PROJECT_MINUS]:
            n = ProjectionExp(n, self.consumeToken())
            n.names.append(self.assertTokenConsume(TK_NAME))
            while self.currentToken[0] == TK_COMMA:
                self.consumeToken()
                n.names.append(self.assertTokenConsume(TK_NAME))
        return n

    def parseMulDivModAndExp(self):
        n = self.parseProjectionExp()
        while self.currentToken[0] in [TK_DIV, TK_MUL, TK_MOD, TK_AND]:
            n = BinaryOpExp(self.consumeToken(), n, self.parseProjectExp())
        return n

    def parsePlusMinusOrExp(self):
        n = self.parseMulDivModAndExp()
        while self.currentToken[0] in [TK_PLUS, TK_MINUS, TK_OR, TK_SET_MINUS]:
            n = BinaryOpExp(self.consumeToken(), n, self.parseMulDivModAndExp())
        return n

    def parseSelectExp(self):
        n = self.parsePlusMinusOrExp()
        while self.currentToken[0] == TK_QUESTION:
            n = BinaryOpExp(self.consumeToken(), n, self.parsePlusMinusOrExp())
        return n

    def parseCompareExp(self):
        n = self.parseSelectExp()
        if self.currentToken[0] in [TK_EQUAL, TK_DIFFERENT, 
                                    TK_LESS, TK_GREATER, 
                                    TK_LESSEQUAL, TK_GREATEREQUAL, TK_TILDE]:
            n = BinaryOpExp(self.consumeToken(), n, self.parseSelectExp())
        return n

    def parseSequenceExp(self):
        self.pushRecover(RecoverSEMICOLON)
        n=None
        try:
            n=self.parseCompareExp()
            if not self.currentToken[0] in thingsThatMayComeAfterParseExp:
                self.parseError("Unexpected token")
                self.recover()
        except RecoverSEMICOLON:
            pass
        finally:
            self.popRecover(RecoverSEMICOLON)
        if self.currentToken[0] == TK_SEMICOLON:
            n2 = n
            n = SequenceExp()
            if n2: n.sequence.append(n2)
        while self.currentToken[0] == TK_SEMICOLON:
            self.consumeToken()
            self.pushRecover(RecoverSEMICOLON)
            try:
                n.sequence.append(self.parseCompareExp())
                if not self.currentToken[0] in thingsThatMayComeAfterParseExp:
                    self.parseError("Unexpected token")
                    self.recover()
            except RecoverSEMICOLON:
                pass
            finally:
                self.popRecover(RecoverSEMICOLON)
        return n

    def parseExp(self):
        return self.parseSequenceExp()     
            
    def parse(self):
        n = None
        self.pushRecover(RecoverEOF)
        try:
            n = self.parseExp()
            self.assertTokenConsume(TK_EOF)
        except RecoverEOF:
            n = SequenceExp()
        finally:
            self.popRecover(RecoverEOF)
        return n

    def assertToken(self, TK):
        if TK != self.currentToken[0]:
            self.parseError("Expected %s at "%tokenNames[TK]);
            self.recover()

    def assertTokenConsume(self, TK):
        token=self.currentToken
        self.assertToken(TK)
        self.consumeToken()
        return token
