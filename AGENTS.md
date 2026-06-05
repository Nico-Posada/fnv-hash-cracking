# Repository Guidelines

## Overview

This is a high-performance library meant to make it easy to attempt to crack/find hash collisions for strings/data hashed with the fnv-1a hashing algorithm. The end goal is to have it also live in an easy-to-install python library for easy usage.

A better description of the repository can be found in the README.md

## Coding Styles

Keeping code minimal and readable is a serious goal of mine. I absolutely DESPISE comments that overexplain trivial things, and especially code with over-the-top guardrails that are useless and add no value to the code.

Another thing that is absolutely banned from this repository is using non-ascii characters ANYWHERE. I don't care how "nice" you may think it looks, never use them. On that same note, never use em/en-dashes. This also means no using em-dashes like `--` in the code, just use a different punctuation.

Try to mimic the coding style that's already present in this repository.

I am a serious believer in DRY (Don't Repeat Yourself). If you see that some of your code is being repeated in multiple places, try to break it off into a private helper function.

## Python Ecosystem

This project is compatible with `uv` (as all things should be)

To run tests, run `uv run pytest`

## Commits

To keeep the git history clean, we will use conventional commit messages from now on. Use the `conventional-commits` skill to see how to make them.