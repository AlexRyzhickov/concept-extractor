from __future__ import annotations

import argparse
from pathlib import Path
from textwrap import dedent

try:
    from concept_extractor.graph import PersonExtractor
except (ModuleNotFoundError, ImportError) as exc:
    raise SystemExit(
        dedent(
            """
            Cannot import 'concept_extractor.graph.PersonExtractor'.
            This package contains a compiled C++ extension and must be installed first.

            From repository root run:
              python3 -m pip install -e ./concept_extractor

            Then run this example again with:
              python3 examples/example.py --dict-dir /path/to/opcorpora-parsed
            """
        ).strip()
    ) from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run person concept extraction for sample passages."
    )
    parser.add_argument(
        "--dict-dir",
        required=False,
        default=None,
        help=(
            "Path to prepared dictionary directory "
            "(contains person_forms.txt, known_forms.txt, lemma_map.txt)."
        ),
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    default_dict_dir = repo_root / "assets" / "opcorpora-parsed"
    dict_dir = Path(args.dict_dir).expanduser().resolve() if args.dict_dir else default_dict_dir

    required_files = ("person_forms.txt", "known_forms.txt", "lemma_map.txt")
    missing = [name for name in required_files if not (dict_dir / name).is_file()]
    if missing:
        raise SystemExit(
            dedent(
                f"""
                Dictionary directory is invalid: {dict_dir}
                Missing files: {", ".join(missing)}

                Use a prepared dictionary directory, for example:
                  python3 examples/example.py --dict-dir {default_dict_dir}
                """
            ).strip()
        )

    extractor = PersonExtractor(str(dict_dir))

    passages = [
        "Встреча с Алексеем Черниковым в Санкт-Петербурге.",
        "Мария Иванова и Петр Сидоров подготовили отчет.",
    ]
    concepts = extractor.extract_batch(passages)

    print("Extracted concepts:")
    for concept in concepts:
        print(
            f"- id={concept.id} normalized={concept.normalized!r} "
            f"original={concept.original!r} passage_index={concept.passage_index}"
        )


if __name__ == "__main__":
    main()
