from __future__ import annotations

import os
import logging
from typing import Sequence
from dataclasses import dataclass

from . import _graph

log = logging.getLogger(__name__)


@dataclass
class Concept:
    id: int
    normalized: str
    original: str
    passage_index: int


class PersonExtractor:
    """Extracts person names from text passages.

    Constructor loads pre-prepared txt files from dict_dir (fast, <1s).
    Use prepare_dictionary() once to create them from OpenCorpora XML.
    """

    def __init__(
        self,
        dict_dir: str,
    ) -> None:
        self._core = _graph.PersonExtractor(
            dict_dir,
        )

    @staticmethod
    def prepare_dictionary(xml_path: str, output_dir: str) -> None:
        """Parse OpenCorpora XML and write txt files to output_dir.

        Extracts everything potentially useful for current and future
        concept extraction. The full parse takes ~50s; run once.

        Files created (current usage marked with *):

        Entity type wordform sets (one lowercased word per line):
            * person_forms.txt   — Name + Surn + Patr combined
            * geo_forms.txt      — Geox (geographical names)
              name_forms.txt     — Name only (first names: александр, мария)
              surname_forms.txt  — Surn only (surnames: путин, иванов)
              patron_forms.txt   — Patr only (patronymics: владимирович)
              org_forms.txt      — Orgn (organization names: газпром)
              trade_forms.txt    — Trad (trademarks/brands: адидас)

        POS wordform sets (one lowercased word per line):
              noun_forms.txt     — NOUN wordforms
              adj_forms.txt      — ADJF + ADJS wordforms
              verb_forms.txt     — VERB + INFN wordforms

        Known vocabulary:
            * known_forms.txt    — ALL wordforms in dictionary

        Lemmatization (tab-separated: wordform\\tlemma):
            * lemma_map.txt      — for Name/Surn/Patr only
              lemma_map_full.txt — for ALL wordforms

        Full lemma metadata (tab-separated):
              lemmas.tsv         — lemma_id\\tlemma\\tpos\\ttags
                                   (e.g. "1\\tабажур\\tNOUN\\tNOUN,inan,masc")

        Inter-lemma links (tab-separated):
              links.tsv          — from_id\\tto_id\\tlink_type
                                   (derivational relations between lemmas)

        Args:
            xml_path: Path to dict.opcorpora.xml (plain or .bz2)
            output_dir: Directory to write txt files to (created if missing).
        """
        import bz2
        import time
        import xml.etree.ElementTree as ET

        os.makedirs(output_dir, exist_ok=True)

        # ── Entity type sets ───────────────────────────────────────────────
        name_forms: set[str] = set()  # Name
        surname_forms: set[str] = set()  # Surn
        patron_forms: set[str] = set()  # Patr
        geo_forms: set[str] = set()  # Geox
        org_forms: set[str] = set()  # Orgn
        trade_forms: set[str] = set()  # Trad

        # ── POS sets ──────────────────────────────────────────────────────
        noun_forms: set[str] = set()  # NOUN
        adj_forms: set[str] = set()  # ADJF, ADJS
        verb_forms: set[str] = set()  # VERB, INFN

        # ── Combined ──────────────────────────────────────────────────────
        person_forms: set[str] = set()  # Name + Surn + Patr
        known_forms: set[str] = set()  # all wordforms

        # ── Lemmatization ─────────────────────────────────────────────────
        lemma_map_person: dict[str, str] = {}  # form → lemma (person only)
        lemma_map_full: dict[str, str] = {}  # form → lemma (all)

        # ── Lemma metadata ────────────────────────────────────────────────
        lemma_rows: list[str] = []  # "id\ttext\tpos\ttags"

        # ── Links ─────────────────────────────────────────────────────────
        link_rows: list[str] = []  # "from_id\tto_id\tlink_type"
        link_types: dict[str, str] = {}  # type_id → type_name

        person_tags = {"Name", "Surn", "Patr"}  # noqa: F841
        noun_tags = {"NOUN"}
        adj_tags = {"ADJF", "ADJS"}
        verb_tags = {"VERB", "INFN"}

        lemma_count = 0
        t0 = time.time()

        if xml_path.endswith(".bz2"):
            source = bz2.open(xml_path, "rt", encoding="utf-8")
        else:
            source = open(xml_path, "r", encoding="utf-8")

        with source:
            for _, elem in ET.iterparse(source, events=("end",)):

                # ── Link types ────────────────────────────────────────────
                if elem.tag == "type":
                    type_id = elem.get("id", "")
                    type_name = (elem.text or "").strip()
                    if type_id and type_name:
                        link_types[type_id] = type_name
                    elem.clear()
                    continue

                # ── Links ─────────────────────────────────────────────────
                if elem.tag == "link":
                    from_id = elem.get("from", "")
                    to_id = elem.get("to", "")
                    type_id = elem.get("type", "")
                    type_name = link_types.get(type_id, type_id)
                    if from_id and to_id:
                        link_rows.append(f"{from_id}\t{to_id}\t{type_name}")
                    elem.clear()
                    continue

                # ── Lemmas ────────────────────────────────────────────────
                if elem.tag != "lemma":
                    continue

                lemma_count += 1
                lemma_id = elem.get("id", "")

                l_elem = elem.find("l")
                if l_elem is None:
                    elem.clear()
                    continue

                lemma_text = (l_elem.get("t") or "").lower()
                if not lemma_text:
                    elem.clear()
                    continue

                tags = {g.get("v", "") for g in l_elem.findall("g")}
                tags.discard("")

                # Lemma metadata row
                pos = ""
                for t in tags:
                    if t in (
                        "NOUN",
                        "ADJF",
                        "ADJS",
                        "COMP",
                        "VERB",
                        "INFN",
                        "PRTF",
                        "PRTS",
                        "GRND",
                        "NUMR",
                        "ADVB",
                        "NPRO",
                        "PRED",
                        "PREP",
                        "CONJ",
                        "PRCL",
                        "INTJ",
                    ):
                        pos = t
                        break
                lemma_rows.append(
                    f"{lemma_id}\t{lemma_text}\t{pos}\t{','.join(sorted(tags))}"
                )

                # Classify
                is_name = "Name" in tags
                is_surn = "Surn" in tags
                is_patr = "Patr" in tags
                is_person = is_name or is_surn or is_patr
                is_geo = "Geox" in tags
                is_org = "Orgn" in tags
                is_trade = "Trad" in tags
                is_noun = bool(tags & noun_tags)
                is_adj = bool(tags & adj_tags)
                is_verb = bool(tags & verb_tags)

                # Collect all wordforms
                forms = [lemma_text]
                for f_elem in elem.findall("f"):
                    form = (f_elem.get("t") or "").lower()
                    if form:
                        forms.append(form)

                for form in forms:
                    known_forms.add(form)
                    lemma_map_full[form] = lemma_text

                # Entity types
                if is_name:
                    for form in forms:
                        name_forms.add(form)
                if is_surn:
                    for form in forms:
                        surname_forms.add(form)
                if is_patr:
                    for form in forms:
                        patron_forms.add(form)
                if is_person:
                    for form in forms:
                        person_forms.add(form)
                        lemma_map_person[form] = lemma_text
                if is_geo:
                    for form in forms:
                        geo_forms.add(form)
                if is_org:
                    for form in forms:
                        org_forms.add(form)
                if is_trade:
                    for form in forms:
                        trade_forms.add(form)

                # POS
                if is_noun:
                    for form in forms:
                        noun_forms.add(form)
                if is_adj:
                    for form in forms:
                        adj_forms.add(form)
                if is_verb:
                    for form in forms:
                        verb_forms.add(form)

                elem.clear()

        elapsed = time.time() - t0
        log.info(
            "Parsed %d lemmas in %.1fs: "
            "%d name, %d surname, %d patron, %d geo, %d org, %d trade, "
            "%d noun, %d adj, %d verb, %d known, %d links",
            lemma_count,
            elapsed,
            len(name_forms),
            len(surname_forms),
            len(patron_forms),
            len(geo_forms),
            len(org_forms),
            len(trade_forms),
            len(noun_forms),
            len(adj_forms),
            len(verb_forms),
            len(known_forms),
            len(link_rows),
        )

        # ── Write files ───────────────────────────────────────────────────

        def _write_set(name: str, data: set[str]) -> None:
            path = os.path.join(output_dir, name)
            with open(path, "w", encoding="utf-8") as f:
                for item in sorted(data):
                    f.write(item + "\n")
            log.info("  %s: %d entries", name, len(data))

        def _write_map(name: str, data: dict[str, str]) -> None:
            path = os.path.join(output_dir, name)
            with open(path, "w", encoding="utf-8") as f:
                for k, v in sorted(data.items()):
                    f.write(k + "\t" + v + "\n")
            log.info("  %s: %d entries", name, len(data))

        def _write_rows(name: str, rows: list[str]) -> None:
            path = os.path.join(output_dir, name)
            with open(path, "w", encoding="utf-8") as f:
                for row in rows:
                    f.write(row + "\n")
            log.info("  %s: %d rows", name, len(rows))

        log.info("Writing to %s ...", output_dir)

        # Entity types (individual)
        _write_set("name_forms.txt", name_forms)
        _write_set("surname_forms.txt", surname_forms)
        _write_set("patron_forms.txt", patron_forms)
        _write_set("geo_forms.txt", geo_forms)
        _write_set("org_forms.txt", org_forms)
        _write_set("trade_forms.txt", trade_forms)

        # Combined (used by C++ PersonExtractor)
        _write_set("person_forms.txt", person_forms)
        _write_set("known_forms.txt", known_forms)

        # POS
        _write_set("noun_forms.txt", noun_forms)
        _write_set("adj_forms.txt", adj_forms)
        _write_set("verb_forms.txt", verb_forms)

        # Lemmatization
        _write_map("lemma_map.txt", lemma_map_person)
        _write_map("lemma_map_full.txt", lemma_map_full)

        # Metadata
        _write_rows("lemmas.tsv", lemma_rows)
        _write_rows("links.tsv", link_rows)

    def extract_batch(self, passages: Sequence[str]) -> list[Concept]:
        """Extract person names from passages. All work in C++, GIL released."""
        raw = self._core.extract_persons(list(passages))
        return [
            Concept(
                id=c.id,
                normalized=c.normalized,
                original=c.original,
                passage_index=c.passage_index,
            )
            for c in raw
        ]
