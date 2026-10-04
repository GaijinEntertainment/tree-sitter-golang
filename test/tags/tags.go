//go:build ignore

package main
//      ^ definition.module

// Shape has an area.
type Shape interface {
//   ^ definition.interface
	Area() float64
	// <- definition.method
}

type (
	// Point is a position.
	Point struct{ X, Y int }
	// <- definition.class
	ID    int
	// <- definition.class
	Alias = Point
	// <- definition.class
	Stringer interface{ String() string }
	// <- definition.interface
	//                  ^ definition.method
)

const Pi, E = 3.14, 2.71
//    ^ definition.constant
//        ^ definition.constant

// Area returns zero.
func (p Point) Area() float64 { return 0 }
//             ^ definition.method

func Map[T any](xs []T) []T { return xs }
//   ^ definition.function

func main() {
//   ^ definition.function
	p := Point{X: 1}
	//   ^ reference.class
	q := geom.Point{}
	//        ^ reference.class
	r := List[int]{}
	//   ^ reference.class
	_ = p.Area()
	//    ^ reference.call
	_ = Map[int](nil)
	//  ^ reference.call
	_ = slices.Sorted[[]int](r)
	//         ^ reference.call
	helper()
	// <- reference.call
	fmt.Println(len(q.X))
	//  ^ reference.call
}
