import sys
from jinja2 import Template

def generate_compose(num_tests: int):
    with open('docker-compose.yaml.j2') as f:
        template = Template(f.read())

    rendered_content = template.render(num_tests=num_tests)

    with open('docker-compose.yaml', 'w') as f:
        f.write(rendered_content)
    
    print(f"docker-compose.yaml generated with {num_tests} test containers.")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python generate_compose.py <number_of_tests>")
        sys.exit(1)
    
    num_tests = int(sys.argv[1])
    generate_compose(num_tests)