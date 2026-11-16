import unittest

from keywords import parse_todo

CASES = [
    ("To do, buy milk.", "buy milk."),
    ("todo: call Sam", "call Sam"),
    ("TO-DO buy eggs", "buy eggs"),
    ("To do list for Saturday.", "list for Saturday."),
    ("  To do,   buy milk", "buy milk"),
    ("To do; buy milk", "buy milk"),
    ("To do! Buy milk", "Buy milk"),
    ("To do… buy milk", "buy milk"),
    ("Today I bought milk.", None),
    ("To-dos for the week", None),
    ("To double-check the invoice, ask Sam.", None),
    ("I have a lot to do.", None),
    ("To do.", None),
    ("To do", None),
    ("", None),
    # Fillers
    ("Uh to do I need to get bath towel mats", "I need to get bath towel mats"),
    ("Um, to do buy milk", "buy milk"),
    ("Uhm... So, okay, to do: call Sam", "call Sam"),
    ("Hmm. Well, to-do uh buy um eggs", "buy eggs"),
    ("To do, er, ask Sam about the umbrella", "ask Sam about the umbrella"),
    ("So I like eggs", None),
    ("Okay, today I bought milk.", None),
    ("Uh.", None),
    ("Um to do", None),
]


class ParseTodoTest(unittest.TestCase):
    def test_cases(self):
        for text, expected in CASES:
            with self.subTest(text=text):
                self.assertEqual(parse_todo(text), expected)


if __name__ == "__main__":
    unittest.main()
