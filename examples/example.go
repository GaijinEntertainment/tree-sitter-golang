//go:build ignore

// Command example lists the words of its input with their counts, most frequent first.
package main

import (
	"bufio"
	"cmp"
	"fmt"
	"iter"
	"os"
	"slices"
	"strings"
)

type Counter[K comparable] struct {
	counts map[K]int
	order  []K
}

type WordCounter = Counter[string]

type Limits struct{ Top int }

type Options struct {
	Limits
	Fold *bool
}

func NewCounter[K comparable]() *Counter[K] {
	return &Counter[K]{counts: make(map[K]int)}
}

func (c *Counter[K]) Add(key K) {
	if _, ok := c.counts[key]; !ok {
		c.order = append(c.order, key)
	}
	c.counts[key]++
}

func (c *Counter[K]) Sorted[V cmp.Ordered](weight func(K, int) V) iter.Seq2[K, int] {
	keys := slices.Clone(c.order)
	slices.SortStableFunc(keys, func(a, b K) int {
		return cmp.Compare(weight(b, c.counts[b]), weight(a, c.counts[a]))
	})
	return func(yield func(K, int) bool) {
		for _, key := range keys {
			if !yield(key, c.counts[key]) {
				return
			}
		}
	}
}

func main() {
	opts := Options{Top: 10, Fold: new(true)}
	words := NewCounter[string]()
	scanner := bufio.NewScanner(os.Stdin)
	scanner.Split(bufio.ScanWords)
	for scanner.Scan() {
		word := scanner.Text()
		if *opts.Fold {
			word = strings.ToLower(word)
		}
		words.Add(word)
	}

	shown := 0
	for word, count := range words.Sorted(func(_ string, n int) int { return n }) {
		if shown == opts.Top {
			break
		}
		fmt.Printf("%6d %s\n", count, word)
		shown++
	}
}
