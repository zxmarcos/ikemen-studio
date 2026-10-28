// gen_names writes the code editor's lists of Ikemen GO's state controllers (with the parameters
// each one reads), triggers (with the names some take in parentheses: GetHitVar(fall.damage)) and
// statedef parameters, read from the engine's compiler, as tools/debugger/src/ikemen_names.inc.
//
//	go run ./tools/debugger/scripts/gen_names src tools/debugger/src/ikemen_names.inc
//
// Spellings: a controller's is its compiler method's (afterImage), a trigger's the Lua function's
// of the same name when there is one (animElemTime), else the lower case the compiler matches.
package main

import (
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

var funcs = map[string]*ast.FuncDecl{} // the CharCompiler methods

func isIdent(e ast.Expr, name string) bool {
	id, ok := e.(*ast.Ident)
	return ok && id.Name == name
}

// The method's parameter named prefix (a parameter's name is prefix + "name"), -1 none
func prefixIndex(fd *ast.FuncDecl) int {
	i := 0
	for _, f := range fd.Type.Params.List {
		for _, n := range f.Names {
			if n.Name == "prefix" {
				return i
			}
			i++
		}
	}
	return -1
}

// A string the expression always is: a literal, prefix, or a sum of them
func str(e ast.Expr, prefix string, hasPrefix bool) (string, bool) {
	switch v := e.(type) {
	case *ast.BasicLit:
		if v.Kind == token.STRING {
			s, err := strconv.Unquote(v.Value)
			return s, err == nil
		}
	case *ast.Ident:
		if hasPrefix && v.Name == "prefix" {
			return prefix, true
		}
	case *ast.BinaryExpr:
		if v.Op == token.ADD {
			a, ok1 := str(v.X, prefix, hasPrefix)
			b, ok2 := str(v.Y, prefix, hasPrefix)
			return a + b, ok1 && ok2
		}
	case *ast.ParenExpr:
		return str(v.X, prefix, hasPrefix)
	}
	return "", false
}

func validParam(s string) bool {
	if s == "" {
		return false
	}
	for _, r := range s {
		if !(r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r == '.' || r == '_') {
			return false
		}
	}
	return true
}

// The parameters a method reads from its section (is), following the methods it hands it to
func params(name, prefix string, seen map[string]bool, out map[string]bool) {
	fd := funcs[name]
	if fd == nil || fd.Body == nil || seen[name+"|"+prefix] {
		return
	}
	seen[name+"|"+prefix] = true
	hasPrefix := prefixIndex(fd) >= 0
	ast.Inspect(fd.Body, func(n ast.Node) bool {
		switch v := n.(type) {
		case *ast.CallExpr:
			sel, ok := v.Fun.(*ast.SelectorExpr)
			if !ok || len(v.Args) == 0 {
				return true
			}
			if isIdent(sel.X, "is") {
				// is.ReadI32("name", ...)
				if s, ok := str(v.Args[0], prefix, hasPrefix); ok && validParam(s) {
					out[s] = true
				}
				return true
			}
			if !isIdent(sel.X, "c") || !isIdent(v.Args[0], "is") {
				return true
			}
			callee := funcs[sel.Sel.Name]
			if callee == nil {
				return true
			}
			if pi := prefixIndex(callee); pi >= 0 {
				if pi < len(v.Args) {
					if s, ok := str(v.Args[pi], prefix, hasPrefix); ok {
						params(sel.Sel.Name, s, seen, out)
					}
				}
				return true
			}
			// A reader of one parameter (paramValue, stateParam...): its first string
			if strings.HasPrefix(sel.Sel.Name, "param") || sel.Sel.Name == "stateParam" {
				for _, a := range v.Args[1:] {
					if s, ok := str(a, prefix, hasPrefix); ok {
						if validParam(s) {
							out[s] = true
						}
						break
					}
				}
			}
			params(sel.Sel.Name, "", seen, out)
		case *ast.IndexExpr:
			// is["name"]
			if isIdent(v.X, "is") {
				if s, ok := str(v.Index, prefix, hasPrefix); ok && validParam(s) {
					out[s] = true
				}
			}
		}
		return true
	})
}

func sorted(m map[string]bool) []string {
	var s []string
	for k := range m {
		s = append(s, k)
	}
	sort.Strings(s)
	return s
}

func isCToken(e ast.Expr) bool {
	sel, ok := e.(*ast.SelectorExpr)
	return ok && isIdent(sel.X, "c") && sel.Sel.Name == "token"
}

func caseNames(cc *ast.CaseClause) []string {
	var names []string
	for _, e := range cc.List {
		if lit, ok := e.(*ast.BasicLit); ok && lit.Kind == token.STRING {
			s, _ := strconv.Unquote(lit.Value)
			if s != "" && s[0] >= 'a' && s[0] <= 'z' {
				names = append(names, s)
			}
		}
	}
	return names
}

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: gen_names <engine src dir> <output .inc>")
		os.Exit(2)
	}
	fset := token.NewFileSet()
	var files []*ast.File
	for _, n := range []string{"compiler.go", "compiler_functions.go", "script.go"} {
		f, err := parser.ParseFile(fset, filepath.Join(os.Args[1], n), nil, 0)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		files = append(files, f)
		for _, d := range f.Decls {
			fd, ok := d.(*ast.FuncDecl)
			if !ok || fd.Recv == nil || len(fd.Recv.List) == 0 {
				continue
			}
			if star, ok := fd.Recv.List[0].Type.(*ast.StarExpr); ok && isIdent(star.X, "CharCompiler") {
				funcs[fd.Name.Name] = fd
			}
		}
	}

	// The controllers: c.scmap's keys and methods; the Lua functions' spellings
	controllers := map[string]string{}
	lua := map[string]string{}
	for _, f := range files {
		ast.Inspect(f, func(n ast.Node) bool {
			switch v := n.(type) {
			case *ast.AssignStmt:
				if sel, ok := v.Lhs[0].(*ast.SelectorExpr); ok && sel.Sel.Name == "scmap" && len(v.Rhs) == 1 {
					if cl, ok := v.Rhs[0].(*ast.CompositeLit); ok {
						for _, e := range cl.Elts {
							kv := e.(*ast.KeyValueExpr)
							key, _ := strconv.Unquote(kv.Key.(*ast.BasicLit).Value)
							if m, ok := kv.Value.(*ast.SelectorExpr); ok {
								controllers[key] = m.Sel.Name
							}
						}
					}
				}
			case *ast.CallExpr:
				if isIdent(v.Fun, "luaRegister") && len(v.Args) >= 2 {
					if lit, ok := v.Args[1].(*ast.BasicLit); ok {
						s, _ := strconv.Unquote(lit.Value)
						lua[strings.ToLower(s)] = s
					}
				}
			}
			return true
		})
	}

	var b strings.Builder
	b.WriteString("// Generated by tools/debugger/scripts/gen_names from the engine's src/compiler.go,\n" +
		"// compiler_functions.go and script.go: do not edit.\n" +
		"// go run ./tools/debugger/scripts/gen_names src tools/debugger/src/ikemen_names.inc\n\n")
	b.WriteString("/* The state controllers, each with the parameters it reads (space separated). */\n")
	b.WriteString("static const GenName kGenControllers[] = {\n")
	var keys []string
	for k := range controllers {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		m := controllers[k]
		name := m
		if strings.ToLower(m) != k {
			name = k
		}
		out := map[string]bool{}
		params(m, "", map[string]bool{}, out)
		fmt.Fprintf(&b, "    { %q, %q },\n", name, strings.Join(sorted(out), " "))
	}
	b.WriteString("};\n\n")

	// The triggers: the cases of expValue's switch on c.token, and the names a nested switch
	// on c.token takes (GetHitVar's, Const's...)
	b.WriteString("/* The triggers, each with the names it takes in parentheses (space separated). */\n")
	b.WriteString("static const GenName kGenTriggers[] = {\n")
	var top *ast.SwitchStmt
	if fd := funcs["expValue"]; fd != nil {
		ast.Inspect(fd.Body, func(n ast.Node) bool {
			if sw, ok := n.(*ast.SwitchStmt); ok && isCToken(sw.Tag) && (top == nil || len(sw.Body.List) > len(top.Body.List)) {
				top = sw
			}
			return true
		})
	}
	triggers := map[string]map[string]bool{}
	if top != nil {
		for _, s := range top.Body.List {
			cc := s.(*ast.CaseClause)
			args := map[string]bool{}
			for _, st := range cc.Body {
				ast.Inspect(st, func(n ast.Node) bool {
					if sw, ok := n.(*ast.SwitchStmt); ok && isCToken(sw.Tag) {
						for _, s2 := range sw.Body.List {
							for _, a := range caseNames(s2.(*ast.CaseClause)) {
								args[a] = true
							}
						}
					}
					return true
				})
			}
			for _, name := range caseNames(cc) {
				triggers[name] = args
			}
		}
	}
	var tkeys []string
	for k := range triggers {
		tkeys = append(tkeys, k)
	}
	sort.Strings(tkeys)
	for _, k := range tkeys {
		name := k
		if s, ok := lua[k]; ok {
			name = s
		}
		fmt.Fprintf(&b, "    { %q, %q },\n", name, strings.Join(sorted(triggers[k]), " "))
	}
	b.WriteString("};\n\n")

	sd := map[string]bool{}
	params("stateDef", "", map[string]bool{}, sd)
	fmt.Fprintf(&b, "/* The parameters of a statedef. */\nstatic const char kGenStatedefParams[] = %q;\n", strings.Join(sorted(sd), " "))

	if err := os.WriteFile(os.Args[2], []byte(b.String()), 0o644); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Printf("%d controllers, %d triggers, %d statedef parameters\n", len(keys), len(tkeys), len(sd))
}
