import unittest

from keywords import PROJECTS_NOTE, parse

CASES = [
    # to do
    ("To do, buy milk.", ("to do", "buy milk")),
    ("todo: call Sam", ("to do", "call Sam")),
    ("TO-DO buy eggs", ("to do", "buy eggs")),
    ("To do list for Saturday.", ("to do", "list for Saturday")),
    ("To do buy milk, eggs...", ("to do", "buy milk, eggs...")),
    ("To do check the SEO audit for blue dot.", ("to do", "check the SEO audit for blue dot")),
    ("  To do,   buy milk", ("to do", "buy milk")),
    ("To do; buy milk", ("to do", "buy milk")),
    ("To do! Buy milk", ("to do", "Buy milk")),
    ("To do… buy milk", ("to do", "buy milk")),
    ("Today I bought milk.", None),
    ("To-dos for the week", None),
    ("To double-check the invoice, ask Sam.", None),
    ("I have a lot to do.", None),
    ("To do.", None),
    ("To do", None),
    ("", None),
    # note: free text, so the final period stays
    ("Note, the meeting moved to 3.", ("note", "the meeting moved to 3.")),
    ("note: call the vet", ("note", "call the vet")),
    ("Notes from the meeting", None),
    ("Notebook is full.", None),
    ("Noted.", None),
    ("I need to note that.", None),
    ("Note.", None),
    # project: a list item, so the final period goes
    ("Project, try the new parser.", ("project", "try the new parser")),
    ("Project: a CLI for Keep", ("project", "a CLI for Keep")),
    ("Projects are hard.", None),
    ("Projected costs are up.", None),
    ("My project is late.", None),
    # Fillers
    ("Uh to do I need to get bath towel mats", ("to do", "I need to get bath towel mats")),
    ("Um, to do buy milk", ("to do", "buy milk")),
    ("Uhm... So, okay, to do: call Sam", ("to do", "call Sam")),
    ("Hmm. Well, to-do uh buy um eggs", ("to do", "buy eggs")),
    ("To do, er, ask Sam about the umbrella", ("to do", "ask Sam about the umbrella")),
    ("Um, note, the vet called.", ("note", "the vet called.")),
    ("Okay, project: voice diary", ("project", "voice diary")),
    ("So I like eggs", None),
    ("Okay, today I bought milk.", None),
    ("Uh.", None),
    ("Um to do", None),
]


def result(text):
    match = parse(text)
    return None if match is None else (match.keyword.name, match.text)


class ParseTest(unittest.TestCase):
    def test_cases(self):
        for text, expected in CASES:
            with self.subTest(text=text):
                self.assertEqual(result(text), expected)

    def test_routing(self):
        cases = {
            "To do, buy milk": ("gtasks-sync", {}),
            "Note, buy milk": ("keep-sync", {}),
            "Project, buy milk": ("keep-sync", {"append_to": PROJECTS_NOTE}),
        }
        for text, expected in cases.items():
            with self.subTest(text=text):
                match = parse(text)
                assert match is not None
                self.assertEqual((match.keyword.service, match.keyword.fields), expected)


if __name__ == "__main__":
    unittest.main()
