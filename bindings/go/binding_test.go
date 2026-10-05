package tree_sitter_golang_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_golang "github.com/GaijinEntertainment/tree-sitter-golang/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	parser := tree_sitter.NewParser()
	defer parser.Close()

	if err := parser.SetLanguage(tree_sitter.NewLanguage(tree_sitter_golang.Language())); err != nil {
		t.Errorf("Error loading Go grammar: %v", err)
	}
}
