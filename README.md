# tree-sitter-golang

[![CI][ci]](https://github.com/GaijinEntertainment/tree-sitter-golang/actions/workflows/ci.yml)
[![crates][crates]](https://crates.io/crates/tree-sitter-golang)
[![npm][npm]](https://www.npmjs.com/package/@gaijin/tree-sitter-golang)
[![pypi][pypi]](https://pypi.org/project/tree-sitter-golang)

Go grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter), at the language version of Go 1.27.

The grammar follows the Go specification and the parsers of the Go release: it parses what `go/parser` and the
compiler's parser accept, with the tree structure of `go/ast`, and reports an error wherever a grammar can see that one
of them rejects the input. It does not do the checks of the type checker.

A text that does not start with a package clause is a fragment, such as a code block of a document: the grammar parses
its import declarations, declarations and statements in any order.

## Versioning

The version of the grammar is `X.Y.P`. It is not a semantic version.

```text
1.27.0
|    |
|    +-- P: the number of the grammar release for that Go version; the first release is 0
+------- X.Y: the newest Go version that the grammar covers
```

- `X.Y` is the newest Go language version that the grammar covers. Each `1.27.P` covers Go 1.27, and the first release
  for Go 1.28 is `1.28.0`.
- `P` is the grammar's own number. It counts the releases of the grammar for one Go version, and it does not follow the
  patch number of a Go release. `1.27.1` is the second release of the grammar for Go 1.27. It is not a grammar for the
  Go release 1.27.1.

A new `P` is a new release of the grammar for the same Go version: a correction of the trees or of the queries. A new
`X.Y` adds the syntax of a newer Go version. Go stays at major version 1, so the version cannot mark a breaking change:
a release adds node kinds and fields, and does not rename or remove one.

## References

- [The Go Programming Language Specification](https://go.dev/ref/spec)
- [Package go/parser](https://pkg.go.dev/go/parser)

[ci]: https://img.shields.io/github/actions/workflow/status/GaijinEntertainment/tree-sitter-golang/ci.yml?logo=github&label=CI
[crates]: https://img.shields.io/crates/v/tree-sitter-golang?logo=rust
[npm]: https://img.shields.io/npm/v/@gaijin/tree-sitter-golang?logo=npm
[pypi]: https://img.shields.io/pypi/v/tree-sitter-golang?logo=pypi&logoColor=ffd242
