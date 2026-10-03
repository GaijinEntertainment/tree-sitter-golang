package tree_sitter_golang_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_golang "github.com/GaijinEntertainment/tree-sitter-golang/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_golang.Language())
	if language == nil {
		t.Errorf("Error loading Go grammar")
	}
}
