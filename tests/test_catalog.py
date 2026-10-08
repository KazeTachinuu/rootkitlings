from pathlib import Path
import tomllib
import unittest

from lib.catalog import Catalog


ROOT = Path(__file__).resolve().parent.parent


class GeneratedSerialTests(unittest.TestCase):
    def setUp(self) -> None:
        self.catalog = Catalog(ROOT)

    def test_course_does_not_store_aliases(self) -> None:
        course = tomllib.loads((ROOT / "course.toml").read_text(encoding="utf-8"))
        self.assertNotIn("aliases", course)

    def test_serials_follow_route_and_kind(self) -> None:
        prefixes = {"lesson": "L", "milestone": "M", "capstone": "C"}
        counters = {kind: 0 for kind in prefixes}
        for stable_id in self.catalog.required_route:
            exercise = self.catalog.by_id[stable_id]
            expected = f"{prefixes[exercise.kind]}{counters[exercise.kind]:02d}"
            self.assertEqual(expected, exercise.serial)
            self.assertEqual(exercise, self.catalog.resolve(expected))
            counters[exercise.kind] += 1

    def test_discovery_order_does_not_define_serials(self) -> None:
        route_positions = {
            stable_id: position
            for position, stable_id in enumerate(self.catalog.required_route)
        }
        ordered = sorted(
            self.catalog.exercises,
            key=lambda exercise: route_positions.get(exercise.id, len(route_positions)),
        )
        required = [exercise for exercise in ordered if exercise.status == "required"]
        self.assertEqual(list(self.catalog.required_route), [item.id for item in required])

    def test_selectors_accept_exact_aliases_and_unique_prefixes(self) -> None:
        self.assertEqual("module-lifecycle", self.catalog.resolve("l00").id)
        self.assertEqual("module-lifecycle", self.catalog.resolve("module-lif").id)

    def test_ambiguous_prefix_lists_matches(self) -> None:
        with self.assertRaisesRegex(ValueError, r"ambiguous exercise 'C0'.*C00.*C09"):
            self.catalog.resolve("C0")


if __name__ == "__main__":
    unittest.main()
