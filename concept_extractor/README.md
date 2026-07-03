# Concept Extractor

C++ person-name concept extractor split out from `RAG/ragz`.

## What Is Included

- C++ graph extractor sources and headers in `cpp/src/graph` and `cpp/include/graph`
- pybind11 binding in `cpp/bindings/graph.cpp`
- Python wrapper in `concept_extractor/graph`
- Russian Snowball stemmer sources in `cpp/third_party/libstemmer_ru`
- self-contained C++ rule test in `cpp/tests`
- DVC pointers for OpenCorpora graph assets in `assets/graph`

Tokenization code, old wheel releases, generated `pybuild` outputs, and unrelated tests are intentionally not copied.

## Build C++ Tests

```sh
cmake -S . -B build -DBUILD_PYTHON=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Build Python Package

```sh
pip install -v --no-build-isolation .
```

## Install From PyPI

```sh
pip install concept-extractor
```

## Use From Python

```python
from concept_extractor.graph import PersonExtractor

extractor = PersonExtractor("assets/graph/opcorpora-parsed")
concepts = extractor.extract_batch(["Встреча с Алексеем Черниковым."])
```

## Run Example

```sh
cd ..
python3 -m pip install -e ./concept_extractor
python3 examples/example.py
# or explicitly:
python3 examples/example.py --dict-dir ./assets/opcorpora-parsed
```

## Assets

Large OpenCorpora files are tracked via DVC pointers:

```sh
dvc pull assets/graph/opcorpora-parsed.tar.dvc
tar -xf assets/graph/opcorpora-parsed.tar -C assets/graph
```
