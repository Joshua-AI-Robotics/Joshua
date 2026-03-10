"""MJX MDP term libraries.

Each module provides *factory functions* that capture static config
(body IDs, thresholds, etc.) and return pure-JAX callables suitable
for ``jax.vmap`` / ``jax.jit``.

Term signatures (returned callables)::

    RewardFn:      (data, state, action) -> scalar
    ObservationFn: (data, state) -> 1-D array
    TerminationFn: (data, state) -> bool scalar
    ResetFn:       (mjx_model, init_data, rng) -> (data, target_pos)
"""
