# usage_from_gdscript.gd — How to use Kuroko from GDScript
#
# This file demonstrates all the ways to interact with Kuroko scripts.

extends Node

func _ready():
	# --- 1. Direct expression evaluation ---
	var result = Kuroko.execute("2 ** 10")
	print("2^10 = ", result)  # 1024

	result = Kuroko.execute("'hello ' * 3")
	print(result)  # "hello hello hello "

	result = Kuroko.execute("[x * x for x in range(5)]")
	print(result)  # [0, 1, 4, 9, 16]

	result = Kuroko.execute("{'name': 'Godot', 'version': 3}")
	print(result)  # {name: Godot, version: 3}

	# --- 2. Execute a script file ---
	result = Kuroko.execute_file("res://examples/hello.krk")
	print("Script returned: ", result)  # 42

	# --- 3. Load an annotated script as a resource ---
	var player_script = load("res://examples/player.krk")
	print("Extends: ", player_script.get_extends_class())  # "KinematicBody2D"
	print("Is tool: ", player_script.is_tool())  # true

	# --- 4. Create an instance and interact with it ---
	var inst = player_script.instance_create_ref(self)
	print("Initialized: ", inst.is_initialized())  # true

	# Read exported properties
	print("Speed: ", inst.get_property("speed"))  # 200.0
	print("Health: ", inst.get_property("max_health"))  # 5

	# Modify properties
	inst.set_property("speed", 300.0)
	print("New speed: ", inst.get_property("speed"))  # 300.0

	# Call methods
	var hp = inst.call_method("take_damage", [2])
	print("HP after damage: ", hp)  # 3
	hp = inst.call_method("heal", [1])
	print("HP after heal: ", hp)  # 4

	# --- 5. Data-driven config ---
	var config = load("res://examples/enemy_config.krk")
	var enemy = config.instance_create_ref(null)
	var stats = enemy.call_method("scale_for_level", [3])
	print("Enemy stats: ", stats)
	# {'hp': 60, 'attack': 7.25, 'name': 'Goblin Lv.3', 'type': 'melee'}

	var loot = enemy.call_method("get_loot_table", [5])
	print("Loot: ", loot)
	# ['potion', 'gold', 'rare_gem', 'legendary_sword']

	# --- 6. Multi-line scripts with functions ---
	var code = """
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

fibonacci(10)
"""
	result = Kuroko.execute(code)
	print("Fibonacci(10) = ", result)  # 55
