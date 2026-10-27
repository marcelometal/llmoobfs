# SPDX-License-Identifier: LGPL-2.1-or-later
"""Client library for MoobFS: typed attributes, publishing, and queries that use QUERY2 when
the layer serves it and the same answers from a directory walk when it does not.

Experimental, 0.x: the API may change between minor versions. Not in the layer or its UAPI
(ADR-0022, ADR-0023).
"""
from .attrs import Value, get_attrs, name_ok, remove_attr, set_attr, validate
from .domain import Domain, get_info, layer_mounted, usable_info
from .publish import file_mode_for, publish
from .query import KernelUnavailable, Match, Order, QueryRefused, Term

__version__ = "0.1.0"

__all__ = ["Domain", "KernelUnavailable", "Match", "Order", "QueryRefused", "Term", "Value",
           "file_mode_for", "get_attrs", "get_info", "layer_mounted", "name_ok", "publish",
           "remove_attr", "set_attr", "usable_info", "validate"]
