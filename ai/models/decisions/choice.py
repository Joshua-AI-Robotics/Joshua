import math


def _probability(value):
    return type(value) in (int, float) and 0 <= value <= 1


def decode_choice(name, choices, answer, threshold):
    if not isinstance(answer, dict) or answer.get("name") != name:
        raise ValueError("mismatched_answer")
    if answer.get("type") == "refusal":
        raise ValueError("refused")
    label = answer.get("choice")
    labels = set(choices)
    if (
        answer.get("type") != "choice"
        or not isinstance(label, str)
        or label not in labels
    ):
        raise ValueError("malformed_response")
    probabilities = answer.get("probabilities")
    if not isinstance(probabilities, list) or len(probabilities) != len(labels):
        raise ValueError("malformed_response")
    total = 0.0
    for entry in probabilities:
        if (
            not isinstance(entry, dict)
            or not isinstance(entry.get("value"), str)
            or entry["value"] not in labels
        ):
            raise ValueError("malformed_response")
        probability = entry.get("probability")
        if not _probability(probability):
            raise ValueError("malformed_response")
        total += probability
        labels.remove(entry["value"])
    confidence = answer.get("confidence")
    if not _probability(confidence) or not math.isclose(total, 1.0, abs_tol=1e-5):
        raise ValueError("malformed_response")
    if confidence < threshold:
        raise ValueError("low_confidence")
    return label
