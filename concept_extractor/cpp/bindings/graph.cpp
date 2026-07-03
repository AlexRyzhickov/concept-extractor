#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <vector>

#include "graph/person_extractor.h"

namespace py = pybind11;

PYBIND11_MODULE(_graph, m) {
    m.doc() = "concept extractor graph core";

    py::class_<ExtractedConcept>(m, "ExtractedConcept")
        .def_readonly("id", &ExtractedConcept::id)
        .def_readonly("normalized", &ExtractedConcept::normalized)
        .def_readonly("original", &ExtractedConcept::original)
        .def_readonly("passage_index", &ExtractedConcept::passage_index);

    py::class_<PersonExtractor>(m, "PersonExtractor")
        .def(
            py::init<const std::string&, std::vector<std::string>>(), py::arg("dict_dir"),
            py::arg("title_stop_words") = std::vector<std::string>{}
        )
        .def(
            "extract_persons", &PersonExtractor::ExtractPersons, py::arg("passages"),
            py::call_guard<py::gil_scoped_release>()
        );
}
