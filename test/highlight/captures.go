//go:build ignore
// <- keyword.directive

package main
// <- keyword.import
//      ^ module

import (
// <- keyword.import
	str "strings"
	// <- module
	//  ^ string
	. "math"
	// <- punctuation.special
	//^ string
	_ "embed"
	// <- variable.builtin
	//^ string
)

// Pi is a constant.
// <- comment
const Pi = 3.14
// <- keyword
//    ^ constant
//       ^ operator
//         ^ number.float

var count int = 0x1F
// <- keyword
//  ^ variable
//        ^ type.builtin
//              ^ number

type Point struct {
// <- keyword.type
//   ^ type.definition
//         ^ keyword.type
	X, Y int `json:"x"`
	// <- variable.member
	//   ^ type.builtin
	//       ^ string
}

type Reader = io.Reader
// <- keyword.type
//   ^ type.definition
//            ^ module
//              ^ punctuation.delimiter
//               ^ type

type Number interface {
//   ^ type.definition
//          ^ keyword.type
	~int | ~float64
	// <- operator
	//   ^ operator
	//      ^ type.builtin
	Value() float64
	// <- function.method
	//      ^ type.builtin
}

func Sum[T Number](values ...T) (total T) {
// <- keyword.function
//   ^ function
//       ^ type
//         ^ type
//                 ^ variable.parameter
//                        ^ operator
//                               ^ variable.parameter
	for _, v := range values {
	// <- keyword.repeat
	//  ^ variable.builtin
	//       ^ operator
	//          ^ keyword.repeat
		total += v
		// <- variable
		//    ^ operator
	}
	return
	// <- keyword.return
}

func (p *Point) Move(dx int) {
// <- keyword.function
//    ^ variable.parameter
//      ^ operator
//       ^ type
//              ^ function.method
//                   ^ variable.parameter
	p.X += dx
	// <- variable
	//^ variable.member
}

func main() {
//   ^ function
	ch := make(chan string, 1)
	//    ^ function.builtin
	//         ^ keyword.type
	//              ^ type.builtin
	go func() { ch <- "done\n" }()
	// <- keyword.coroutine
	//          ^ variable
	//             ^ operator
	//                     ^ string.escape
	defer close(ch)
	// <- keyword
	//    ^ function.builtin
	if len(ch) == 0 {
	// <- keyword.conditional
	// ^ function.builtin
	//         ^ operator
	} else {
	//^ keyword.conditional
	}
	switch v := any(1).(type) {
	// <- keyword.conditional
	//                  ^ keyword.type
	case nil:
	// <- keyword.conditional
	//   ^ constant.builtin
		_ = v
	default:
	// <- keyword.conditional
	}
	r := 'x'
	//   ^ character
	ok := true && false
	//    ^ boolean
	//         ^ operator
	//            ^ boolean
	q := new(Point)
	//   ^ function.builtin
	//       ^ variable
	q.Move(1)
	//^ function.method.call
	str.ToUpper("x")
	//  ^ function.method.call
	println(r, ok, nil)
	// <- function.builtin
	//             ^ constant.builtin
	use(r)
	// <- function.call
outer:
// <- label
	for {
		break outer
		// <- keyword
		//    ^ label
	}
	select {
	// <- keyword.conditional
	case <-ch:
	// <- keyword.conditional
	//   ^ operator
	}
	x := []int{1, 2}[0:1]
	//     ^ type.builtin
	//          ^ punctuation.delimiter
	//              ^ punctuation.bracket
	_ = x
	_ = 2i
	//  ^ number
	_ = `raw`
	//  ^ string
}
