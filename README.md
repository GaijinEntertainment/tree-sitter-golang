# tree-sitter-golang

[![CI][ci]](https://github.com/GaijinEntertainment/tree-sitter-golang/actions/workflows/ci.yml)
[![crates][crates]](https://crates.io/crates/tree-sitter-golang)
[![pypi][pypi]](https://pypi.org/project/tree-sitter-golang)

Go grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter), at the language version of Go 1.27.

The grammar follows the Go specification and the parsers of the Go release: it parses what `go/parser` and the
compiler's parser accept, with the tree structure of `go/ast`, and reports an error wherever a grammar can see that one
of them rejects the input. It does not do the checks of the type checker.

A text that does not start with a package clause is a fragment, such as a code block of a document: the grammar parses
its import declarations, declarations and statements in any order.

## References

- [The Go Programming Language Specification](https://go.dev/ref/spec)
- [Package go/parser](https://pkg.go.dev/go/parser)

[ci]: https://img.shields.io/github/actions/workflow/status/GaijinEntertainment/tree-sitter-golang/ci.yml?logo=github&label=CI
[crates]: https://img.shields.io/crates/v/tree-sitter-golang?logo=rust
[pypi]: https://img.shields.io/pypi/v/tree-sitter-golang?logo=pypi&logoColor=ffd242
