/*
 * Demo program for minijvm. Deliberately sticks to the supported subset:
 * static methods, int arithmetic, branches, loops, recursion, and
 * System.out.print/println with int or String literal arguments.
 * (No string concatenation: javac compiles that to invokedynamic.)
 */
public class Test {

    static int square(int x) {
        return x * x;
    }

    static int fib(int n) {
        if (n < 2) {
            return n;
        }
        return fib(n - 1) + fib(n - 2);
    }

    static int gcd(int a, int b) {
        while (b != 0) {
            int t = a % b;
            a = b;
            b = t;
        }
        return a;
    }

    public static void main(String[] args) {
        System.out.println("Hello from minijvm!");

        System.out.println("squares of 1..5:");
        for (int i = 1; i <= 5; i++) {
            System.out.print("  ");
            System.out.println(square(i));
        }

        System.out.print("fib(10) = ");
        System.out.println(fib(10));

        System.out.print("gcd(1071, 462) = ");
        System.out.println(gcd(1071, 462));

        int sum = 0;
        for (int i = 1; i <= 100; i++) {
            sum += i;
        }
        System.out.print("sum 1..100 = ");
        System.out.println(sum);
    }
}
