import XCTest
import SwiftTreeSitter
import TreeSitterGolang

final class TreeSitterGolangTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_golang())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Go grammar")
    }
}
